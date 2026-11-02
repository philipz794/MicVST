#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "state/PluginScanCache.h"
#include "audio/ScanCoordinator.h"
#include "audio/Vst3ClassScan.h"

// Hilfsfunktion: eine plausible PluginDescription bauen (kein echtes Plugin nötig).
static juce::PluginDescription makeDesc (const juce::String& name, const juce::String& file)
{
    juce::PluginDescription d;
    d.name = name; d.pluginFormatName = "VST3"; d.fileOrIdentifier = file;
    d.uniqueId = 0x1234; d.category = "Fx"; d.manufacturerName = "Test";
    return d;
}

struct PluginScanCacheTest : juce::UnitTest
{
    PluginScanCacheTest() : juce::UnitTest ("PluginScanCache") {}
    void runTest() override
    {
        beginTest ("round-trip preserves types and skip list");
        {
            juce::KnownPluginList list;
            list.addType (makeDesc ("Pro-Q 3", "C:/VST3/Pro-Q 3.vst3"));
            juce::Array<SkippedPlugin> skips;
            skips.add ({ "C:/VST3/Broken.vst3", "unresponsive", 1234567 });

            auto xml = PluginScanCache::toXml (list, skips);
            expect (xml != nullptr);

            juce::KnownPluginList list2;
            juce::Array<SkippedPlugin> skips2;
            expect (PluginScanCache::fromXml (*xml, list2, skips2));
            expectEquals (list2.getNumTypes(), 1);
            expectEquals (list2.getTypes()[0].name, juce::String ("Pro-Q 3"));
            expectEquals (skips2.size(), 1);
            expectEquals (skips2[0].file, juce::String ("C:/VST3/Broken.vst3"));
            expectEquals (skips2[0].reason, juce::String ("unresponsive"));
            expect (skips2[0].fileTimeMs == 1234567);
        }

        beginTest ("foreign xml is rejected");
        {
            juce::XmlElement wrong ("SomethingElse");
            juce::KnownPluginList list; juce::Array<SkippedPlugin> skips;
            expect (! PluginScanCache::fromXml (wrong, list, skips));
            expectEquals (list.getNumTypes(), 0);
        }

        beginTest ("load deletes corrupt file and returns false");
        {
            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getNonexistentChildFile ("micvst_cache_test", ".xml");
            f.replaceWithText ("this is not xml");
            juce::KnownPluginList list; juce::Array<SkippedPlugin> skips;
            expect (! PluginScanCache::load (f, list, skips));
            expect (! f.existsAsFile());
        }
    }
};
static PluginScanCacheTest pluginScanCacheTest;

// Fake-Runner: liefert pro Datei ein vorkonfiguriertes Ergebnis; respektiert shouldSkip.
struct FakeRunner : ScanProcessRunner
{
    std::map<juce::String, ScanProcessResult> results;
    juce::StringArray calls;
    ScanProcessResult run (const juce::String& path, int, std::function<bool()>,
                           std::function<bool()> shouldSkip) override
    {
        calls.add (path);
        if (shouldSkip())
            return { ScanProcessResult::Status::skippedByUser, {} };
        auto it = results.find (path);
        return it != results.end() ? it->second : ScanProcessResult{};
    }
};

static juce::String scanResultXmlFor (const juce::PluginDescription& d)
{
    juce::XmlElement root ("MicVSTScanResult");
    root.addChildElement (d.createXml().release());
    return root.toString();
}

struct RunScanTest : juce::UnitTest
{
    RunScanTest() : juce::UnitTest ("runScan") {}
    void runTest() override
    {
        beginTest ("ok/timeout/failed are sorted into found and skipped");
        {
            FakeRunner r;
            r.results["C:/v/Good.vst3"] = { ScanProcessResult::Status::ok,
                                            scanResultXmlFor (makeDesc ("Good", "C:/v/Good.vst3")) };
            r.results["C:/v/Hang.vst3"] = { ScanProcessResult::Status::timeout, {} };
            r.results["C:/v/Bad.vst3"]  = { ScanProcessResult::Status::failed, {} };

            juce::StringArray progressNames; int lastTotal = 0;
            auto out = runScan ({ "C:/v/Good.vst3", "C:/v/Hang.vst3", "C:/v/Bad.vst3" }, r, 1000,
                                [&] (int, int total, const juce::String& n) { progressNames.add (n); lastTotal = total; },
                                [] { return false; });

            expect (out.completed);
            expectEquals (out.found.size(), 1);
            expectEquals (out.found[0].name, juce::String ("Good"));
            expectEquals (out.skipped.size(), 2);
            expectEquals (out.skipped[0].reason, juce::String ("unresponsive"));
            expectEquals (out.skipped[1].reason, juce::String ("failed"));
            expectEquals (progressNames.size(), 3);
            expectEquals (lastTotal, 3);
        }

        beginTest ("garbage xml on ok-status counts as failed");
        {
            FakeRunner r;
            r.results["C:/v/Garbage.vst3"] = { ScanProcessResult::Status::ok, "not xml" };
            auto out = runScan ({ "C:/v/Garbage.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 0);
            expectEquals (out.skipped.size(), 1);
            expectEquals (out.skipped[0].reason, juce::String ("failed"));
        }

        beginTest ("shouldExit aborts without blacklisting");
        {
            FakeRunner r;
            auto out = runScan ({ "C:/v/A.vst3", "C:/v/B.vst3" }, r, 1000, {},
                                [] { return true; });   // sofort abbrechen
            expect (! out.completed);
            expectEquals ((int) r.calls.size(), 0);
            expectEquals (out.skipped.size(), 0);
        }

        beginTest ("user skip only affects the current file and is reset per file");
        {
            FakeRunner r;
            r.results["C:/v/A.vst3"] = { ScanProcessResult::Status::ok,
                                         scanResultXmlFor (makeDesc ("A", "C:/v/A.vst3")) };
            r.results["C:/v/C.vst3"] = { ScanProcessResult::Status::ok,
                                         scanResultXmlFor (makeDesc ("C", "C:/v/C.vst3")) };
            std::atomic<bool> skipRequest { false };
            // Skip-Wunsch beim Fortschritts-Callback von Datei B setzen (wie ein UI-Klick).
            auto out = runScan ({ "C:/v/A.vst3", "C:/v/B.vst3", "C:/v/C.vst3" }, r, 1000,
                                [&] (int, int, const juce::String& n)
                                { if (n == "B") skipRequest.store (true); },
                                [] { return false; }, &skipRequest);
            expect (out.completed);
            expectEquals (out.found.size(), 2);
            expectEquals (out.skipped.size(), 1);
            expectEquals (out.skipped[0].file, juce::String ("C:/v/B.vst3"));
            expectEquals (out.skipped[0].reason, juce::String ("skipped"));
        }

        beginTest ("partial: rescued types are kept, file skip-listed with crash reason");
        {
            FakeRunner r;
            juce::XmlElement root ("MicVSTScanResult");
            root.setAttribute ("crashCode", "0xC0000005");
            root.addChildElement (makeDesc ("W1", "C:/v/Shell.vst3").createXml().release());
            root.addChildElement (makeDesc ("W2", "C:/v/Shell.vst3").createXml().release());
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::partial;
            pr.resultXmlText = root.toString();
            pr.exitCode = 3;
            r.results["C:/v/Shell.vst3"] = pr;

            auto out = runScan ({ "C:/v/Shell.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 2);
            expectEquals (out.skipped.size(), 1);
            expectEquals (out.skipped[0].reason,
                          juce::String ("crashed (2 plugin(s) rescued, 0xC0000005)"));
        }

        beginTest ("failed with crash exit code carries the code in the reason");
        {
            FakeRunner r;
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::failed;
            pr.exitCode = 0xC0000005u;
            r.results["C:/v/Dead.vst3"] = pr;
            auto out = runScan ({ "C:/v/Dead.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.skipped[0].reason, juce::String ("failed (exit 0xC0000005)"));
        }

        beginTest ("plain failed (own exit codes) keeps the short reason");
        {
            FakeRunner r;
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::failed;
            pr.exitCode = 1;   // Kind: keine Typen gefunden -> kein Crash
            r.results["C:/v/NotAPlugin.vst3"] = pr;
            auto out = runScan ({ "C:/v/NotAPlugin.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.skipped[0].reason, juce::String ("failed"));
        }

        beginTest ("failed status with rescueable XML rescues the crash and carries the code");
        {
            // Zweiter Crash beim Kind-Teardown (DLL_PROCESS_DETACH): Exit-Code ist ein
            // NTSTATUS statt 3, obwohl die Ergebnisdatei vollständig geschrieben wurde.
            FakeRunner r;
            juce::XmlElement root ("MicVSTScanResult");
            root.setAttribute ("crashCode", "0xC0000005");
            root.addChildElement (makeDesc ("Teardown", "C:/v/Teardown.vst3").createXml().release());
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::failed;
            pr.exitCode = 0xC0000005u;
            pr.resultXmlText = root.toString();
            r.results["C:/v/Teardown.vst3"] = pr;

            auto out = runScan ({ "C:/v/Teardown.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 1);
            expectEquals (out.skipped.size(), 1);
            expectEquals (out.skipped[0].reason,
                          juce::String ("crashed (1 plugin(s) rescued, 0xC0000005)"));
        }

        beginTest ("partial with valid root but zero rescued children still carries the crash code");
        {
            FakeRunner r;
            juce::XmlElement root ("MicVSTScanResult");
            root.setAttribute ("crashCode", "0xC0000005");
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::partial;
            pr.resultXmlText = root.toString();
            pr.exitCode = 3;
            r.results["C:/v/EmptyRescue.vst3"] = pr;

            auto out = runScan ({ "C:/v/EmptyRescue.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 0);
            expectEquals (out.skipped.size(), 1);
            expectEquals (out.skipped[0].reason,
                          juce::String ("crashed (0 plugin(s) rescued, 0xC0000005)"));
        }

        beginTest ("partial with malformed xml falls back to plain failed, exit 3 not decorated");
        {
            FakeRunner r;
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::partial;
            pr.resultXmlText = "not xml";
            pr.exitCode = 3;
            r.results["C:/v/Malformed.vst3"] = pr;

            auto out = runScan ({ "C:/v/Malformed.vst3" }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 0);
            expectEquals (out.skipped.size(), 1);
            expectEquals (out.skipped[0].reason, juce::String ("failed"));
        }

        beginTest ("found descriptions are re-keyed to the scanned path");
        {
            // JUCE liefert für Bundle-ORDNER OHNE moduleinfo.json die INNERE Binary als
            // fileOrIdentifier (getLibraryPaths -> recursiveFileSearch steigt ins Bundle).
            // MicVST sucht im Cache aber nach dem gescannten Pfad -> getTypeForFile fände
            // nie etwas und die Datei würde bei JEDEM Start erneut gescannt. Verifiziert
            // mit einem echten Bundle (rnnoise ohne moduleinfo.json).
            FakeRunner r;
            const juce::String scanned  = "C:/v/Bundly.vst3";
            const juce::String innerDll = "C:/v/Bundly.vst3/Contents/x86_64-win/Bundly.vst3";
            r.results[scanned] = { ScanProcessResult::Status::ok,
                                   scanResultXmlFor (makeDesc ("Bundly", innerDll)) };

            auto out = runScan ({ scanned }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 1);
            expectEquals (out.found[0].fileOrIdentifier, scanned);
        }

        beginTest ("crash-rescued descriptions are re-keyed to the scanned path too");
        {
            FakeRunner r;
            const juce::String scanned = "C:/v/Shelly.vst3";
            juce::XmlElement root ("MicVSTScanResult");
            root.setAttribute ("crashCode", "0xC0000005");
            root.addChildElement (makeDesc ("S1", "C:/v/Shelly.vst3/Contents/x86_64-win/S.vst3")
                                      .createXml().release());
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::partial;
            pr.resultXmlText = root.toString();
            pr.exitCode = 3;
            r.results[scanned] = pr;

            auto out = runScan ({ scanned }, r, 1000, {}, [] { return false; });
            expectEquals (out.found.size(), 1);
            expectEquals (out.found[0].fileOrIdentifier, scanned);
            expectEquals (out.skipped[0].file, scanned);
        }

        beginTest ("onFileDone delivers one delta per file, carrying only that file's result");
        {
            FakeRunner r;
            r.results["C:/v/A.vst3"]    = { ScanProcessResult::Status::ok,
                                            scanResultXmlFor (makeDesc ("A", "C:/v/A.vst3")) };
            r.results["C:/v/Hang.vst3"] = { ScanProcessResult::Status::timeout, {} };

            juce::Array<ScanOutcome> deltas;
            auto out = runScan ({ "C:/v/A.vst3", "C:/v/Hang.vst3" }, r, 1000, {},
                                [] { return false; }, nullptr,
                                [&] (const ScanOutcome& d) { deltas.add (d); });

            expect (out.completed);
            expectEquals (deltas.size(), 2);
            expectEquals (deltas[0].found.size(), 1);
            expectEquals (deltas[0].found[0].name, juce::String ("A"));
            expectEquals (deltas[0].skipped.size(), 0);
            expectEquals (deltas[1].found.size(), 0);
            expectEquals (deltas[1].skipped.size(), 1);
            expectEquals (deltas[1].skipped[0].file, juce::String ("C:/v/Hang.vst3"));
        }

        beginTest ("crash-rescue delta carries rescued types AND the skip entry together");
        {
            FakeRunner r;
            juce::XmlElement root ("MicVSTScanResult");
            root.setAttribute ("crashCode", "0xC0000005");
            root.addChildElement (makeDesc ("W1", "C:/v/Shell.vst3").createXml().release());
            ScanProcessResult pr;
            pr.status = ScanProcessResult::Status::partial;
            pr.resultXmlText = root.toString();
            pr.exitCode = 3;
            r.results["C:/v/Shell.vst3"] = pr;

            juce::Array<ScanOutcome> deltas;
            runScan ({ "C:/v/Shell.vst3" }, r, 1000, {}, [] { return false; }, nullptr,
                     [&] (const ScanOutcome& d) { deltas.add (d); });

            expectEquals (deltas.size(), 1);
            expectEquals (deltas[0].found.size(), 1);
            expectEquals (deltas[0].skipped.size(), 1);
        }

        beginTest ("abort keeps the deltas of already finished files (the rescan-loop bug)");
        {
            // Der eigentliche Regressionstest: vor dem Fix lieferte ein abgebrochener Scan
            // NICHTS zurück (finishedCb wird bei completed == false nie gerufen) -> der
            // Aufrufer konnte nichts persistieren und scannte beim naechsten Start alles neu.
            FakeRunner r;
            r.results["C:/v/A.vst3"] = { ScanProcessResult::Status::ok,
                                         scanResultXmlFor (makeDesc ("A", "C:/v/A.vst3")) };
            r.results["C:/v/B.vst3"] = { ScanProcessResult::Status::ok,
                                         scanResultXmlFor (makeDesc ("B", "C:/v/B.vst3")) };

            juce::Array<ScanOutcome> deltas;
            // Nach der ersten Datei abbrechen (wie ein App-Quit mitten im Scan).
            auto out = runScan ({ "C:/v/A.vst3", "C:/v/B.vst3" }, r, 1000, {},
                                [&] { return deltas.size() >= 1; }, nullptr,
                                [&] (const ScanOutcome& d) { deltas.add (d); });

            expect (! out.completed);                 // Scan lief NICHT durch
            expectEquals ((int) r.calls.size(), 1);   // B wurde nie angefasst
            expectEquals (deltas.size(), 1);          // A ist trotzdem beim Aufrufer angekommen
            expectEquals (deltas[0].found.size(), 1);
            expectEquals (deltas[0].found[0].name, juce::String ("A"));
        }

        beginTest ("runner-level abort keeps earlier deltas, emits none for the aborted file");
        {
            FakeRunner r;
            r.results["C:/v/A.vst3"]     = { ScanProcessResult::Status::ok,
                                             scanResultXmlFor (makeDesc ("A", "C:/v/A.vst3")) };
            r.results["C:/v/Killed.vst3"] = { ScanProcessResult::Status::aborted, {} };

            juce::Array<ScanOutcome> deltas;
            auto out = runScan ({ "C:/v/A.vst3", "C:/v/Killed.vst3" }, r, 1000, {},
                                [] { return false; }, nullptr,
                                [&] (const ScanOutcome& d) { deltas.add (d); });

            expect (! out.completed);
            expectEquals (deltas.size(), 1);
            expectEquals (deltas[0].found[0].name, juce::String ("A"));
        }
    }
};
static RunScanTest runScanTest;

struct ParseScanResultTest : juce::UnitTest
{
    ParseScanResultTest() : juce::UnitTest ("parseScanResultXml") {}
    void runTest() override
    {
        beginTest ("valid result xml yields descriptions");
        {
            juce::Array<juce::PluginDescription> out;
            expect (parseScanResultXml (scanResultXmlFor (makeDesc ("X", "C:/v/X.vst3")), out));
            expectEquals (out.size(), 1);
            expectEquals (out[0].name, juce::String ("X"));
        }
        beginTest ("garbage and wrong root are rejected");
        {
            juce::Array<juce::PluginDescription> out;
            expect (! parseScanResultXml ("nope", out));
            expect (! parseScanResultXml ("<Wrong/>", out));
            expectEquals (out.size(), 0);
        }
        beginTest ("crashCode attribute is surfaced when requested");
        {
            juce::XmlElement root ("MicVSTScanResult");
            root.setAttribute ("crashCode", "0xDEAD");
            root.addChildElement (makeDesc ("Y", "C:/v/Y.vst3").createXml().release());
            juce::Array<juce::PluginDescription> out;
            juce::String code;
            expect (parseScanResultXml (root.toString(), out, &code));
            expectEquals (code, juce::String ("0xDEAD"));
        }
    }
};
static ParseScanResultTest parseScanResultTest;

struct MergeScanResultsTest : juce::UnitTest
{
    MergeScanResultsTest() : juce::UnitTest ("mergeScanResults") {}
    void runTest() override
    {
        // Echte Temp-Datei: der Re-Stamp liest die mtime vom Dateisystem.
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("micvst_merge", ".vst3");
        tmp.replaceWithText ("fake");
        const auto realTime = tmp.getLastModificationTime();

        beginTest ("found entries get freshly stamped mtime");
        {
            juce::KnownPluginList list;
            auto d = makeDesc ("Fresh", tmp.getFullPathName());
            d.lastFileModTime = juce::Time (1);   // absichtlich falsch (Kind-erfasste stale mtime)
            ScanOutcome o; o.found.add (d);
            mergeScanResults (list, o);
            expectEquals (list.getNumTypes(), 1);
            expect (list.getTypes()[0].lastFileModTime == realTime);
        }

        beginTest ("old entry of same file (different uid) is replaced, others untouched");
        {
            juce::KnownPluginList list;
            auto oldEntry = makeDesc ("Old", tmp.getFullPathName());  oldEntry.uniqueId = 0x1111;
            auto other    = makeDesc ("Other", "C:/v/Other.vst3");    other.uniqueId    = 0x9999;
            list.addType (oldEntry); list.addType (other);

            auto updated = makeDesc ("New", tmp.getFullPathName());   updated.uniqueId  = 0x2222;
            ScanOutcome o; o.found.add (updated);
            mergeScanResults (list, o);

            expectEquals (list.getNumTypes(), 2);   // Old ersetzt, Other bleibt
            expect (list.getTypeForFile (tmp.getFullPathName()) != nullptr);
            expectEquals (list.getTypeForFile (tmp.getFullPathName())->uniqueId, 0x2222);
            expect (list.getTypeForFile ("C:/v/Other.vst3") != nullptr);
        }

        beginTest ("bundle entries are restamped with the inner binary's mtime");
        {
            auto bdir  = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getNonexistentChildFile ("micvst_merge_bundle", ".vst3");
            auto inner = bdir.getChildFile ("Contents").getChildFile ("x86_64-win")
                             .getChildFile ("B.vst3");
            inner.create();
            inner.replaceWithText ("dll");
            expect (inner.setLastModificationTime (juce::Time (5000000)));

            juce::KnownPluginList list;
            auto d = makeDesc ("Bundle", bdir.getFullPathName());
            d.lastFileModTime = juce::Time (1);   // Kind-erfasste (Ordner-)Zeit, absichtlich falsch
            ScanOutcome o; o.found.add (d);
            mergeScanResults (list, o);
            expect (list.getTypes()[0].lastFileModTime == juce::Time (5000000));

            bdir.deleteRecursively();
        }

        tmp.deleteFile();
    }
};
static MergeScanResultsTest mergeScanResultsTest;

struct FilterFilesNeedingScanTest : juce::UnitTest
{
    FilterFilesNeedingScanTest() : juce::UnitTest ("filterFilesNeedingScan") {}
    void runTest() override
    {
        juce::VST3PluginFormat vst3;
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("micvst_filter", "");
        dir.createDirectory();
        auto mk = [&] (const juce::String& fileName)
        {
            auto f = dir.getChildFile (fileName);
            f.replaceWithText ("fake");
            return f;
        };
        auto upToDate = mk ("UpToDate.vst3"), changed = mk ("Changed.vst3"),
             skipped = mk ("Skipped.vst3"), staleSkip = mk ("StaleSkip.vst3");

        juce::KnownPluginList list;
        auto dUp = makeDesc ("Up", upToDate.getFullPathName());
        dUp.lastFileModTime = upToDate.getLastModificationTime();     // aktuell -> kein Rescan
        list.addType (dUp);
        auto dCh = makeDesc ("Ch", changed.getFullPathName());
        dCh.lastFileModTime = juce::Time (1);                          // stale -> Rescan
        dCh.uniqueId = 0x2222;
        list.addType (dCh);

        juce::Array<SkippedPlugin> skips;
        skips.add ({ skipped.getFullPathName(), "failed",
                     skipped.getLastModificationTime().toMilliseconds() });   // aktiv
        skips.add ({ staleSkip.getFullPathName(), "failed", 1 });             // stale -> Retry

        const juce::StringArray all { upToDate.getFullPathName(), changed.getFullPathName(),
                                      skipped.getFullPathName(), staleSkip.getFullPathName() };
        auto out = filterFilesNeedingScan (all, list, vst3, skips);

        beginTest ("cache hit is filtered, mtime mismatch is rescanned");
        expect (! out.contains (upToDate.getFullPathName()));
        expect (out.contains (changed.getFullPathName()));

        beginTest ("active skip filters, stale skip is dropped and rescanned");
        expect (! out.contains (skipped.getFullPathName()));
        expect (out.contains (staleSkip.getFullPathName()));
        expectEquals (skips.size(), 1);   // nur der aktive Skip bleibt
        expectEquals (skips[0].file, skipped.getFullPathName());

        beginTest ("force-list bypasses the cache-is-up-to-date check");
        {
            // Crash-Rescue: Cache-Eintrag ist nach der Rettung schon aktuell gestempelt,
            // forceRescan erzwingt die Datei trotzdem erneut in die Scan-Liste.
            auto forced = mk ("Forced.vst3");
            juce::KnownPluginList forcedList;
            auto dForced = makeDesc ("Forced", forced.getFullPathName());
            dForced.lastFileModTime = effectiveModTime (forced);   // aktuell -> normalerweise kein Rescan
            forcedList.addType (dForced);
            juce::Array<SkippedPlugin> noSkips;

            auto withoutForce = filterFilesNeedingScan ({ forced.getFullPathName() }, forcedList, vst3, noSkips);
            expect (! withoutForce.contains (forced.getFullPathName()));

            auto withForce = filterFilesNeedingScan ({ forced.getFullPathName() }, forcedList, vst3, noSkips,
                                                     { forced.getFullPathName() });
            expect (withForce.contains (forced.getFullPathName()));
        }

        beginTest ("force-list also bypasses the skip filter and keeps the entry for now");
        {
            // "Retry skipped plugins": die Skip-Liste wird NICHT vorab geleert, damit ein
            // Abbruch mitten im Retry die noch nicht versuchten Einträge nicht verliert.
            // Also muss forceRescan den Skip-Filter aushebeln; aufgeräumt wird erst pro
            // Teilergebnis (applySkipDelta).
            auto retried = mk ("Retried.vst3");
            juce::KnownPluginList emptyList;
            juce::Array<SkippedPlugin> skips2;
            skips2.add ({ retried.getFullPathName(), "unresponsive",
                          effectiveModTime (retried).toMilliseconds() });

            auto out2 = filterFilesNeedingScan ({ retried.getFullPathName() }, emptyList, vst3, skips2,
                                                { retried.getFullPathName() });
            expect (out2.contains (retried.getFullPathName()));
            expectEquals (skips2.size(), 1);   // Eintrag bleibt bis zum Teilergebnis stehen
        }

        dir.deleteRecursively();
    }
};
static FilterFilesNeedingScanTest filterFilesNeedingScanTest;

struct Vst3UidHashTest : juce::UnitTest
{
    Vst3UidHashTest() : juce::UnitTest ("vst3 uid hashes") {}
    void runTest() override
    {
        // Golden-Vektoren: cid-Bytes direkt aus der Factory gelesen, Erwartungswerte aus
        // JUCEs EIGENER Ausgabe für dieselben Plugins (Scan-Ergebnis-XML bzw. Cache). Sie
        // nageln die Byte-Reihenfolge fest -- die ist der fehleranfällige Teil, weil FUID
        // auf Windows (COM_COMPATIBLE) die ersten acht Bytes paarweise vertauscht liest
        // und ein naives Big-Endian-Lesen falsche, aber plausible Werte liefert.
        struct Vector { const char* name; const char* cidHex; juce::uint32 uniqueId, deprecatedUid; };

        const Vector vectors[] = {
            // WaveShell1-VST3 17.1 -- Waves kodiert ASCII in die cid (alle Bytes < 0x80)
            { "CLA-76 Mono",   "375453564336634d6c612d3736206d6f", 0xd39f9424, 0xf81fc928 },
            { "X-FDBK Stereo", "41545356534678532d6664626b207374", 0xb46ca8e9, 0xed3e8a11 },
            // FabFilter Pro-Q 3 -- echte GUID mit Bytes >= 0x80 (0xc4, 0x9a, 0xb9, 0x84,
            // 0xb3, 0x9d); prüft die Vorzeichenerweiterung in deprecatedUid
            { "Pro-Q 3",       "71dbc4724d7a9a45b97e51745d84b39d", 0xba5f7ab2, 0x8c4652e9 },
        };

        for (auto& v : vectors)
        {
            beginTest (juce::String ("uid hashes match JUCE for ") + v.name);

            juce::MemoryBlock cid;
            cid.loadFromHexString (v.cidHex);
            expectEquals ((int) cid.getSize(), 16);

            expectEquals ((juce::uint32) vst3UniqueIdFromCid (cid.getData()),     v.uniqueId);
            expectEquals ((juce::uint32) vst3DeprecatedUidFromCid (cid.getData()), v.deprecatedUid);
        }

        beginTest ("byte order actually matters (guards against naive big-endian reads)");
        {
            // Zwei cids, die sich NUR in der Reihenfolge der ersten vier Bytes
            // unterscheiden, müssen verschiedene uniqueIds ergeben -- sonst wäre der
            // COM_COMPATIBLE-Swap wirkungslos implementiert.
            juce::MemoryBlock a, b;
            a.loadFromHexString ("00010203000000000000000000000000");
            b.loadFromHexString ("03020100000000000000000000000000");
            expectEquals ((int) a.getSize(), 16);
            expectEquals ((int) b.getSize(), 16);
            expect (vst3UniqueIdFromCid (a.getData()) != vst3UniqueIdFromCid (b.getData()));
        }
    }
};
static Vst3UidHashTest vst3UidHashTest;

struct ApplySkipDeltaTest : juce::UnitTest
{
    ApplySkipDeltaTest() : juce::UnitTest ("applySkipDelta") {}
    void runTest() override
    {
        auto baseList = []
        {
            juce::Array<SkippedPlugin> s;
            s.add ({ "C:/v/Shell.vst3", "unresponsive", 100 });
            s.add ({ "C:/v/Other.vst3", "failed", 200 });
            return s;
        };

        beginTest ("successful retry drops the stale skip entry");
        {
            auto skipped = baseList();
            ScanOutcome delta; delta.found.add (makeDesc ("W", "C:/v/Shell.vst3"));
            applySkipDelta (skipped, delta);
            expectEquals (skipped.size(), 1);
            expectEquals (skipped[0].file, juce::String ("C:/v/Other.vst3"));
        }

        beginTest ("failing retry replaces the entry instead of duplicating it");
        {
            auto skipped = baseList();
            ScanOutcome delta;
            delta.skipped.add ({ "C:/v/Shell.vst3", "failed (exit 0xC0000005)", 999 });
            applySkipDelta (skipped, delta);
            expectEquals (skipped.size(), 2);
            const auto* shell = std::find_if (skipped.begin(), skipped.end(),
                [] (const SkippedPlugin& s) { return s.file == "C:/v/Shell.vst3"; });
            expect (shell != skipped.end());
            expectEquals (shell->reason, juce::String ("failed (exit 0xC0000005)"));
            expect (shell->fileTimeMs == 999);
        }

        beginTest ("crash-rescue delta (found AND skipped for one file) stays single-entry");
        {
            auto skipped = baseList();
            ScanOutcome delta;
            delta.found.add (makeDesc ("W", "C:/v/Shell.vst3"));
            delta.skipped.add ({ "C:/v/Shell.vst3", "crashed (1 plugin(s) rescued)", 7 });
            applySkipDelta (skipped, delta);
            expectEquals (skipped.size(), 2);
        }

        beginTest ("untouched files keep their entries");
        {
            auto skipped = baseList();
            ScanOutcome delta; delta.found.add (makeDesc ("N", "C:/v/New.vst3"));
            applySkipDelta (skipped, delta);
            expectEquals (skipped.size(), 2);
        }
    }
};
static ApplySkipDeltaTest applySkipDeltaTest;

struct FindTypeForEntryTest : juce::UnitTest
{
    FindTypeForEntryTest() : juce::UnitTest ("findTypeForEntry") {}
    void runTest() override
    {
        // Shell-Plugin: viele Typen hinter EINEM Dateipfad (WaveShell: 347). Ohne die
        // uniqueId liefert KnownPluginList::getTypeForFile immer den ERSTEN Treffer --
        // nach einem Neustart stünde dann ein beliebiges Waves-Plugin in der Kette,
        // und dessen Zustands-Blob würde aufs falsche Plugin angewendet.
        juce::KnownPluginList list;
        const juce::String shell = "C:/v/WaveShell.vst3";
        auto a = makeDesc ("NS1 Mono",   shell); a.uniqueId = 0x1111;
        auto b = makeDesc ("NS1 Stereo", shell); b.uniqueId = 0x2222;
        auto c = makeDesc ("C1 comp",    shell); c.uniqueId = 0x3333;
        list.addType (a); list.addType (b); list.addType (c);

        beginTest ("uniqueId picks the right type out of a shell");
        {
            auto t = findTypeForEntry (list, shell, 0x2222);
            expect (t != nullptr);
            expectEquals (t->name, juce::String ("NS1 Stereo"));

            auto t3 = findTypeForEntry (list, shell, 0x3333);
            expect (t3 != nullptr);
            expectEquals (t3->name, juce::String ("C1 comp"));
        }

        beginTest ("uniqueId 0 (config from an older version) still resolves to that file");
        {
            // WELCHER Typ der Datei dabei herauskommt, ist absichtlich nicht festgelegt:
            // genau diese beliebige Wahl traf vorher auch getTypeForFile, alte Configs
            // verhalten sich also unverändert. Garantiert ist nur: ein Typ DIESER Datei.
            auto t = findTypeForEntry (list, shell, 0);
            expect (t != nullptr);
            expectEquals (t->fileOrIdentifier, shell);
        }

        beginTest ("unknown uniqueId for a known file still resolves to that file");
        {
            // Plugin-Update kann die uid ändern; die Datei ist aber eindeutig da.
            auto t = findTypeForEntry (list, shell, 0x9999);
            expect (t != nullptr);
            expectEquals (t->fileOrIdentifier, shell);
        }

        beginTest ("unknown file resolves to nothing");
        expect (findTypeForEntry (list, "C:/v/Nope.vst3", 0x1111) == nullptr);
    }
};
static FindTypeForEntryTest findTypeForEntryTest;

struct ChainIsResolvableTest : juce::UnitTest
{
    ChainIsResolvableTest() : juce::UnitTest ("chainIsResolvable") {}
    void runTest() override
    {
        juce::KnownPluginList list;
        list.addType (makeDesc ("Known", "C:/v/Known.vst3"));

        auto entry = [] (const juce::String& fileOrId)
        {
            PluginEntryState p; p.fileOrId = fileOrId; return p;
        };

        beginTest ("empty chain needs nothing from the cache");
        expect (chainIsResolvable ({}, list));

        beginTest ("builtin converters never need the cache");
        {
            juce::KnownPluginList empty;
            expect (chainIsResolvable ({ entry ("builtin:mono2stereo"),
                                         entry ("builtin:stereo2mono") }, empty));
        }

        beginTest ("cached plugin resolves, uncached one does not");
        expect (chainIsResolvable ({ entry ("C:/v/Known.vst3") }, list));
        expect (! chainIsResolvable ({ entry ("C:/v/Missing.vst3") }, list));

        beginTest ("a single missing entry blocks the whole chain");
        expect (! chainIsResolvable ({ entry ("builtin:mono2stereo"),
                                       entry ("C:/v/Known.vst3"),
                                       entry ("C:/v/Missing.vst3") }, list));
    }
};
static ChainIsResolvableTest chainIsResolvableTest;

struct PathScopeTest : juce::UnitTest
{
    PathScopeTest() : juce::UnitTest ("pathIsInsideAnyFolder") {}
    void runTest() override
    {
        const juce::StringArray folders { "C:\\Plugins", "D:\\VST3" };
        beginTest ("children match, prefix-siblings do not");
        expect (pathIsInsideAnyFolder ("C:\\Plugins\\X.vst3", folders));
        expect (pathIsInsideAnyFolder ("C:\\Plugins\\Vendor\\Deep\\X.vst3", folders));
        expect (pathIsInsideAnyFolder ("D:\\VST3\\A.vst3", folders));
        expect (! pathIsInsideAnyFolder ("C:\\Plugins2\\X.vst3", folders));   // der alte Bug
        expect (! pathIsInsideAnyFolder ("E:\\Other\\X.vst3", folders));
        beginTest ("case-insensitive on Windows");
        expect (pathIsInsideAnyFolder ("c:\\plugins\\x.vst3", folders));
        beginTest ("empty folder entries are ignored");
        expect (! pathIsInsideAnyFolder ("C:\\Anything\\X.vst3", juce::StringArray { "" }));
    }
};
static PathScopeTest pathScopeTest;

struct EffectiveModTimeTest : juce::UnitTest
{
    EffectiveModTimeTest() : juce::UnitTest ("effectiveModTime") {}
    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("micvst_emt", "");
        dir.createDirectory();

        beginTest ("plain file uses its own mtime");
        {
            auto f = dir.getChildFile ("Plain.vst3");
            f.replaceWithText ("x");
            expect (effectiveModTime (f) == f.getLastModificationTime());
        }

        // Bundle-Ordner mit innerer Binary (echtes VST3-Bundle-Layout).
        auto bundle = dir.getChildFile ("Bundle.vst3");
        auto inner  = bundle.getChildFile ("Contents").getChildFile ("x86_64-win")
                            .getChildFile ("Bundle.vst3");
        inner.create();
        inner.replaceWithText ("dll");
        expect (inner.setLastModificationTime (juce::Time (1000000)));

        beginTest ("bundle dir uses the inner binary's mtime, service writes are ignored");
        {
            expect (effectiveModTime (bundle) == juce::Time (1000000));
            // Companion-Dienst simulieren: Log direkt im Bundle bumpt die ORDNER-mtime.
            bundle.getChildFile ("service.log").replaceWithText ("touched");
            expect (effectiveModTime (bundle) == juce::Time (1000000));
        }

        beginTest ("newest inner binary wins (multi-arch bundle)");
        {
            auto arm = bundle.getChildFile ("Contents").getChildFile ("arm64-win")
                             .getChildFile ("Bundle.vst3");
            arm.create();
            arm.replaceWithText ("dll");
            expect (arm.setLastModificationTime (juce::Time (2000000)));
            expect (effectiveModTime (bundle) == juce::Time (2000000));
        }

        beginTest ("dir without inner binaries falls back to dir mtime");
        {
            auto empty = dir.getChildFile ("Empty.vst3");
            empty.createDirectory();
            expect (effectiveModTime (empty) == empty.getLastModificationTime());
        }

        beginTest ("MicVST3Format: service write no rescan, binary update rescans");
        {
            MicVST3Format fmt;
            juce::PluginDescription d;
            d.fileOrIdentifier = bundle.getFullPathName();
            d.lastFileModTime  = effectiveModTime (bundle);
            bundle.getChildFile ("another.log").replaceWithText ("touched again");
            expect (! fmt.pluginNeedsRescanning (d));
            expect (inner.setLastModificationTime (juce::Time (3000000)));
            expect (fmt.pluginNeedsRescanning (d));
        }

        dir.deleteRecursively();
    }
};
static EffectiveModTimeTest effectiveModTimeTest;
