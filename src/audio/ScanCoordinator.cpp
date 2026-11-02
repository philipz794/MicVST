#include "audio/ScanCoordinator.h"

// Gefundene Typen auf den Pfad umschlüsseln, den WIR gescannt haben. Nötig, weil JUCE für
// Bundle-ORDNER ohne moduleinfo.json die INNERE Binary als fileOrIdentifier zurückgibt
// (getLibraryPaths -> recursiveFileSearch steigt ins Bundle hinab), MicVST den Cache aber
// über den gescannten Pfad befragt: list.getTypeForFile(bundle) fände dann nie etwas und
// filterFilesNeedingScan würde die Datei bei JEDEM Start wieder als "neu" einreihen --
// ein Dauer-Rescan, den kein mtime-Vergleich beheben kann. Zum Laden ist der Ordnerpfad
// gleichwertig: JUCE löst ihn erneut auf und matcht die Klasse über name + uniqueId.
static void rekeyToScannedPath (juce::Array<juce::PluginDescription>& descs,
                                const juce::String& scannedFile)
{
    for (auto& d : descs)
        d.fileOrIdentifier = scannedFile;
}

// Ergebnis EINER Datei in ein Teilergebnis einsortieren. Nimmt r per Referenz, weil der
// ok-Zweig den Status bei unbrauchbarem XML auf failed herunterstuft.
static ScanOutcome classifyScanResult (const juce::String& file, ScanProcessResult& r)
{
    ScanOutcome d;

    if (r.status == ScanProcessResult::Status::ok)
    {
        juce::Array<juce::PluginDescription> descs;
        if (parseScanResultXml (r.resultXmlText, descs))
        {
            rekeyToScannedPath (descs, file);
            d.found.addArray (descs);
            return d;
        }
        r.status = ScanProcessResult::Status::failed;   // ok ohne brauchbares XML = failed
    }

    // Crash-Rescue: gilt sowohl für "partial" (planmäßig, Exit 3 mit geretteter XML) als
    // auch für "failed" mit vorhandener resultXmlText -- der Kindprozess kann NACH dem
    // Schreiben der Ergebnisdatei ein zweites Mal crashen (z. B. eine zweite SEH-Exception
    // beim DLL-Teardown in DLL_PROCESS_DETACH); der Exit-Code ist dann ein NTSTATUS statt 3,
    // obwohl das Ergebnis vollständig auf der Platte steht. Eine Rettung gilt als gültig,
    // wenn entweder Kinder geparst wurden ODER die Root ein crashCode-Attribut trägt
    // (gültige, aber leere Rettung); bei kaputtem/fremdem XML fällt der Code unten in die
    // normale "failed"-Behandlung durch.
    if ((r.status == ScanProcessResult::Status::partial
         || r.status == ScanProcessResult::Status::failed) && r.resultXmlText.isNotEmpty())
    {
        juce::Array<juce::PluginDescription> descs;
        juce::String crashCode;
        const bool parsedOk = parseScanResultXml (r.resultXmlText, descs, &crashCode);
        if (parsedOk || crashCode.isNotEmpty())
        {
            rekeyToScannedPath (descs, file);
            d.found.addArray (descs);
            d.skipped.add ({ file,
                             "crashed (" + juce::String (descs.size()) + " plugin(s) rescued"
                                 + (crashCode.isNotEmpty() ? ", " + crashCode : juce::String()) + ")",
                             effectiveModTime (juce::File (file)).toMilliseconds() });
            return d;
        }
    }

    // Eigene Exit-Codes (0/1/2/3) sind kein Crash; alles darüber ist ein NTSTATUS.
    const auto reason = r.status == ScanProcessResult::Status::timeout       ? juce::String ("unresponsive")
                      : r.status == ScanProcessResult::Status::skippedByUser ? juce::String ("skipped")
                      : r.exitCode > 3
                          ? "failed (exit 0x" + juce::String::toHexString ((int) r.exitCode).toUpperCase() + ")"
                          : juce::String ("failed");
    d.skipped.add ({ file, reason, effectiveModTime (juce::File (file)).toMilliseconds() });
    return d;
}

ScanOutcome runScan (const juce::StringArray& files, ScanProcessRunner& runner, int timeoutMs,
                     std::function<void (int, int, const juce::String&)> progress,
                     std::function<bool()> shouldExit,
                     std::atomic<bool>* skipRequest,
                     std::function<void (const ScanOutcome&)> onFileDone)
{
    ScanOutcome out;
    const int total = files.size();

    for (int i = 0; i < total; ++i)
    {
        if (shouldExit && shouldExit()) return out;   // completed bleibt false

        if (skipRequest != nullptr)
            skipRequest->store (false);   // Skip-Wunsch gilt immer nur für die aktuelle Datei

        const auto& file = files[i];
        if (progress)
            progress (i + 1, total, juce::File (file).getFileNameWithoutExtension());

        auto r = runner.run (file, timeoutMs,
                             shouldExit ? shouldExit : [] { return false; },
                             [skipRequest] { return skipRequest != nullptr && skipRequest->load(); });

        // Abbruch: die Datei hat KEIN verwertbares Ergebnis, also auch kein Teilergebnis
        // melden -- sie darf beim nächsten Start einfach erneut dran sein.
        if (r.status == ScanProcessResult::Status::aborted)
            return out;

        const auto delta = classifyScanResult (file, r);
        out.found.addArray (delta.found);
        out.skipped.addArray (delta.skipped);
        if (onFileDone)
            onFileDone (delta);
    }

    out.completed = true;
    return out;
}

bool parseScanResultXml (const juce::String& xmlText, juce::Array<juce::PluginDescription>& out,
                         juce::String* crashCode)
{
    auto xml = juce::parseXML (xmlText);
    if (xml == nullptr || ! xml->hasTagName ("MicVSTScanResult")) return false;
    if (crashCode != nullptr)
        *crashCode = xml->getStringAttribute ("crashCode");

    bool any = false;
    for (auto* child : xml->getChildIterator())
    {
        juce::PluginDescription d;
        if (d.loadFromXml (*child)) { out.add (d); any = true; }
    }
    return any;
}

juce::StringArray filterFilesNeedingScan (const juce::StringArray& allFiles,
                                          const juce::KnownPluginList& list,
                                          juce::AudioPluginFormat& format,
                                          juce::Array<SkippedPlugin>& skipped,
                                          const juce::StringArray& forceRescan)
{
    // Stale Skips entfernen: Datei geändert (Plugin-Update) -> neuer Versuch.
    for (int i = skipped.size(); --i >= 0;)
    {
        const juce::File f (skipped[i].file);
        if (! f.exists() || effectiveModTime (f).toMilliseconds() != skipped[i].fileTimeMs)
        {
            juce::Logger::writeToLog ("Scan: Skip aufgehoben (Datei geändert/entfernt): " + skipped[i].file);
            skipped.remove (i);
        }
    }

    auto isSkipped = [&] (const juce::String& file)
    {
        for (auto& s : skipped) if (s.file == file) return true;
        return false;
    };

    juce::StringArray out;
    for (auto& f : allFiles)
    {
        // Erzwungene Dateien ("Retry skipped plugins") übergehen beide Filter: den
        // Skip-Filter, weil sie ja genau deswegen dran sind, und den Cache-Check, weil
        // mergeScanResults gerettete Typen SOFORT mit ihrer aktuellen effectiveModTime
        // stempelt -- der Cache-Eintrag sieht danach aktuell aus, obwohl die Datei nach
        // einem Crash-Rescue erneut versucht werden muss.
        const bool forced = forceRescan.contains (f);
        if (! forced && isSkipped (f))
            continue;
        if (! forced && list.isListingUpToDate (f, format))
            continue;
        // Grund + Zeitstempel loggen: macht den Dauer-Rescan-Fall in log.txt remote
        // diagnostizierbar (cache = gespeicherte mtime, datei = aktuelle effectiveModTime).
        juce::String detail;
        if (auto t = list.getTypeForFile (f))
            detail = " (cache=" + juce::String (t->lastFileModTime.toMilliseconds())
                   + " datei=" + juce::String (effectiveModTime (juce::File (f)).toMilliseconds()) + ")";
        juce::Logger::writeToLog (juce::String ("Scan ")
            + (forced ? "(erzwungen): "
                      : list.getTypeForFile (f) == nullptr ? "(neu): " : "(geändert): ") + f + detail);
        out.add (f);
    }
    return out;
}

void mergeScanResults (juce::KnownPluginList& list, const ScanOutcome& outcome)
{
    juce::StringArray scannedFiles;
    for (auto& d : outcome.found)
        scannedFiles.addIfNotAlreadyThere (d.fileOrIdentifier);
    for (auto& t : list.getTypes())
        if (scannedFiles.contains (t.fileOrIdentifier))
            list.removeType (t);

    for (auto d : outcome.found)   // Kopie: lastFileModTime wird neu gestempelt
    {
        d.lastFileModTime = effectiveModTime (juce::File (d.fileOrIdentifier));
        list.addType (d);
    }
}

void applySkipDelta (juce::Array<SkippedPlugin>& skipped, const ScanOutcome& delta)
{
    juce::StringArray touched;
    for (auto& d : delta.found)   touched.addIfNotAlreadyThere (d.fileOrIdentifier);
    for (auto& s : delta.skipped) touched.addIfNotAlreadyThere (s.file);

    for (int i = skipped.size(); --i >= 0;)
        if (touched.contains (skipped[i].file))
            skipped.remove (i);

    skipped.addArray (delta.skipped);
}

std::unique_ptr<juce::PluginDescription> findTypeForEntry (const juce::KnownPluginList& list,
                                                           const juce::String& fileOrId,
                                                           int uniqueId)
{
    std::unique_ptr<juce::PluginDescription> firstForFile;

    for (auto& t : list.getTypes())   // Kopie; daher wird hier kopiert, nicht gezeigt
    {
        if (t.fileOrIdentifier != fileOrId)
            continue;
        if (uniqueId != 0 && t.uniqueId == uniqueId)
            return std::make_unique<juce::PluginDescription> (t);
        if (firstForFile == nullptr)
            firstForFile = std::make_unique<juce::PluginDescription> (t);
    }
    return firstForFile;
}

bool chainIsResolvable (const juce::Array<PluginEntryState>& plugins,
                        const juce::KnownPluginList& list)
{
    for (auto& p : plugins)
        if (! p.fileOrId.startsWith ("builtin:")
            && findTypeForEntry (list, p.fileOrId, p.uniqueId) == nullptr)
            return false;
    return true;
}

bool pathIsInsideAnyFolder (const juce::String& path, const juce::StringArray& folders)
{
    const juce::File f (path);
    for (auto& folder : folders)
        if (folder.isNotEmpty()
            && (f == juce::File (folder) || f.isAChildOf (juce::File (folder))))
            return true;
    return false;
}

juce::Time effectiveModTime (const juce::File& vst3FileOrBundle)
{
    if (! vst3FileOrBundle.isDirectory())
        return vst3FileOrBundle.getLastModificationTime();

    juce::Time newest;
    for (auto& inner : vst3FileOrBundle.findChildFiles (juce::File::findFiles, true, "*.vst3"))
        newest = juce::jmax (newest, inner.getLastModificationTime());
    return newest != juce::Time() ? newest : vst3FileOrBundle.getLastModificationTime();
}

// ============================ echter Kindprozess-Runner ============================

namespace
{
    // Startet MicVST.exe --scan <plugin> --out <tmpfile> und wartet in 100-ms-Schritten,
    // damit App-Ende (shouldAbort) den Kindprozess sofort killen kann.
    struct ChildProcessRunner : ScanProcessRunner
    {
        ScanProcessResult run (const juce::String& pluginPath, int timeoutMs,
                               std::function<bool()> shouldAbort,
                               std::function<bool()> shouldSkip) override
        {
            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
            const auto outFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getNonexistentChildFile ("micvst_scan", ".xml");

            juce::ChildProcess proc;
            if (! proc.start (juce::StringArray { exe.getFullPathName(), "--scan", pluginPath,
                                                  "--out", outFile.getFullPathName() }, 0))
                return { ScanProcessResult::Status::failed, {} };

            // Überlauf-sichere Deadline (getMillisecondCounter wraps nach ~49.7 Tagen)
            const auto startMs = juce::Time::getMillisecondCounter();
            while (proc.isRunning())
            {
                if (shouldAbort()) { proc.kill(); outFile.deleteFile();
                                     return { ScanProcessResult::Status::aborted, {} }; }
                if (shouldSkip()) { proc.kill(); outFile.deleteFile();
                                    return { ScanProcessResult::Status::skippedByUser, {} }; }
                if (juce::Time::getMillisecondCounter() - startMs >= (juce::uint32) timeoutMs)
                {   proc.kill(); outFile.deleteFile();
                    return { ScanProcessResult::Status::timeout, {} }; }
                juce::Thread::sleep (100);
            }

            ScanProcessResult r;
            r.exitCode = proc.getExitCode();
            r.status = r.exitCode == 0 ? ScanProcessResult::Status::ok
                     : r.exitCode == 3 ? ScanProcessResult::Status::partial
                                       : ScanProcessResult::Status::failed;
            // XML IMMER laden, wenn vorhanden -- auch bei "failed": stirbt das Kind ein
            // zweites Mal beim Teardown (DLL_PROCESS_DETACH), ist der Exit-Code ein NTSTATUS
            // statt 3, obwohl die Ergebnisdatei vollständig geschrieben wurde. runScan rettet
            // ein solches Ergebnis dann trotzdem (siehe dortiger Crash-Rescue-Zweig).
            if (outFile.existsAsFile())
                r.resultXmlText = outFile.loadFileAsString();
            outFile.deleteFile();
            return r;
        }
    };
}

ScanCoordinator::ScanCoordinator (juce::StringArray filesToScan,
                                  std::function<void (int, int, juce::String)> onProgress,
                                  std::function<void (ScanOutcome)> onFileScanned,
                                  std::function<void()> onFinished,
                                  int timeoutMsIn,
                                  std::unique_ptr<ScanProcessRunner> runnerOverride)
    : juce::Thread ("PluginScan"),
      files (std::move (filesToScan)),
      progressCb (std::move (onProgress)),
      fileScannedCb (std::move (onFileScanned)),
      finishedCb (std::move (onFinished)),
      timeoutMs (timeoutMsIn),
      runner (runnerOverride != nullptr ? std::move (runnerOverride)
                                        : std::make_unique<ChildProcessRunner>())
{
    startThread();
}

ScanCoordinator::~ScanCoordinator()
{
    // Vor stopThread markieren: bereits via callAsync eingereihte Lambdas (siehe run())
    // prüfen *alive und verwerfen sich selbst, statt den Callback auf einem ggf. schon
    // zerstörten Owner aufzurufen. stopThread kann solche Nachrichten nicht zurückziehen.
    *alive = false;
    stopThread (5000);   // shouldExit -> Runner killt den Kindprozess binnen ~100 ms
}

void ScanCoordinator::run()
{
    auto outcome = runScan (files, *runner, timeoutMs,
        [this] (int cur, int total, const juce::String& name)
        {
            if (progressCb)
                juce::MessageManager::callAsync ([alive = alive, cb = progressCb, cur, total, name]
                                                 { if (*alive) cb (cur, total, name); });
        },
        [this] { return threadShouldExit(); }, &skipRequest,
        [this] (const ScanOutcome& delta)
        {
            if (fileScannedCb)
                juce::MessageManager::callAsync ([alive = alive, cb = fileScannedCb, delta]
                                                 { if (*alive) cb (delta); });
        });

    // Die Nutzdaten sind über fileScannedCb längst draußen; dieser Callback meldet nur
    // noch "komplett durchgelaufen" und kommt dank FIFO-Reihenfolge des Message-Threads
    // garantiert NACH allen Teilergebnissen an.
    if (outcome.completed && finishedCb)
        juce::MessageManager::callAsync ([alive = alive, cb = finishedCb] { if (*alive) cb(); });
}
