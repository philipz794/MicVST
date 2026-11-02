#pragma once
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "state/Persistence.h"
#include "state/PluginScanCache.h"

// Ergebnis eines Kindprozess-Laufs für EINE .vst3-Datei.
struct ScanProcessResult
{
    // partial = Kind crashte mitten in der Enumeration (Exit-Code 3), hat aber die bis
    // dahin gefundenen Typen als XML gerettet (WaveShell & Co.: hunderte Klassen).
    enum class Status { ok, failed, timeout, skippedByUser, aborted, partial };
    Status status = Status::failed;
    juce::String resultXmlText;   // Inhalt der --out-Datei (bei ok und partial)
    juce::uint32 exitCode = 0;    // roher Exit-Code des Kindes (Crash: NTSTATUS)
};

// Injizierbar für Tests. Die echte Implementierung startet "MicVST.exe --scan".
// shouldSkip = User will NUR die aktuelle Datei überspringen (Scan läuft weiter),
// shouldAbort = ganzer Scan soll enden (App-Shutdown).
struct ScanProcessRunner
{
    virtual ~ScanProcessRunner() = default;
    virtual ScanProcessResult run (const juce::String& pluginPath, int timeoutMs,
                                   std::function<bool()> shouldAbort,
                                   std::function<bool()> shouldSkip) = 0;
};

struct ScanOutcome
{
    juce::Array<juce::PluginDescription> found;
    juce::Array<SkippedPlugin> skipped;
    bool completed = false;   // false = per shouldExit abgebrochen (nichts blacklisten!)
};

// Synchroner, testbarer Scan-Kern: Dateien der Reihe nach durch den Runner schicken.
// Timeout -> Skip "unresponsive"; Crash/kaputtes XML -> Skip "failed"; User-Skip via
// skipRequest -> Skip "skipped" (Flag wird pro Datei zurückgesetzt); Abbruch -> nichts.
//
// onFileDone: wird nach JEDER fertig verarbeiteten Datei mit deren eigenem Teilergebnis
// gerufen -- found/skipped enthalten nur diese Datei, das Feld completed ist dort ohne
// Bedeutung. Damit kann der Aufrufer inkrementell persistieren. WICHTIG: Der Rückgabewert
// allein genügt dafür nicht, denn bei Abbruch (shouldExit/Status aborted) kehrt runScan
// vorzeitig zurück und ScanCoordinator ruft dann gar keinen finished-Callback -- ohne
// onFileDone ginge die gesamte Arbeit eines abgebrochenen Scans verloren und der nächste
// Programmstart würde alles erneut scannen (der Dauer-Rescan-Bug bis v1.1.1).
ScanOutcome runScan (const juce::StringArray& files, ScanProcessRunner& runner, int timeoutMs,
                     std::function<void (int, int, const juce::String&)> progress,
                     std::function<bool()> shouldExit,
                     std::atomic<bool>* skipRequest = nullptr,
                     std::function<void (const ScanOutcome&)> onFileDone = {});

// Parst die vom Kindmodus geschriebene Ergebnisdatei (<MicVSTScanResult> mit PLUGIN-
// Kindern). crashCode != nullptr: erhält das Root-Attribut "crashCode" (leer wenn keins).
bool parseScanResultXml (const juce::String& xmlText, juce::Array<juce::PluginDescription>& out,
                         juce::String* crashCode = nullptr);

// Welche Dateien müssen wirklich gescannt werden? Filtert Cache-Treffer und Skips heraus;
// Skip-Einträge, deren Datei sich geändert hat (Update), werden dabei aus skipped entfernt.
// forceRescan ("Retry skipped plugins"): diese Dateien übergehen BEIDE Filter -- den
// Cache-Check ("isListingUpToDate", nötig wegen Crash-Rescue: mergeScanResults stempelt
// gerettete Typen sofort frisch, sonst käme die Datei nie wieder dran) UND den Skip-Filter.
// Ihre Skip-Einträge bleiben dabei absichtlich stehen; abgeräumt werden sie erst pro
// Teilergebnis via applySkipDelta, damit ein Abbruch mitten im Retry die noch nicht
// versuchten Einträge nicht verliert.
juce::StringArray filterFilesNeedingScan (const juce::StringArray& allFiles,
                                          const juce::KnownPluginList& list,
                                          juce::AudioPluginFormat& format,
                                          juce::Array<SkippedPlugin>& skipped,
                                          const juce::StringArray& forceRescan = {});

// Scan-Ergebnisse in die KnownPluginList übernehmen (für ein Teilergebnis einer einzelnen
// Datei ebenso wie für ein Gesamtergebnis). Entfernt vorher Alteinträge
// derselben Dateien (uid kann sich bei Plugin-Updates ändern; ein stehen bleibender
// Alteintrag mit veralteter mtime erzwingt sonst bei jedem Start einen Rescan) und
// stempelt lastFileModTime FRISCH: Plugins (UAD, Acustica, ...) schreiben beim
// Laden/Entladen in ihre Bundles, die im Kindprozess erfasste mtime ist dann schon
// wieder veraltet -> Dauer-Rescan-Schleife. Beim Merge ist der Kindprozess beendet.
void mergeScanResults (juce::KnownPluginList& list, const ScanOutcome& outcome);

// Skip-Liste an ein Teilergebnis anpassen: erst ALLE Alteinträge der von diesem Ergebnis
// abgedeckten Dateien entfernen, dann dessen neue Skips aufnehmen. Ohne das Entfernen
// bliebe eine erfolgreich nachgescannte Datei für immer skip-gelistet, und ein erneut
// fehlschlagender Retry würde Duplikate ansammeln.
void applySkipDelta (juce::Array<SkippedPlugin>& skipped, const ScanOutcome& delta);

// Den Typ zu einem gespeicherten Ketten-Eintrag finden. NICHT durch getTypeForFile zu
// ersetzen: Shell-Plugins liefern viele Typen hinter EINEM Dateipfad (WaveShell: 347), und
// getTypeForFile gibt immer den ersten zurück -- nach einem Neustart stünde dann ein
// beliebiges Plugin der Shell in der Kette. Darum zuerst über file + uniqueId suchen.
// uniqueId == 0 (Config einer älteren Version, die das Feld nicht kannte) oder eine
// unbekannte uniqueId (Plugin-Update hat sie geändert) fallen auf den ersten Treffer der
// Datei zurück -- für Nicht-Shell-Plugins ist das genau richtig.
std::unique_ptr<juce::PluginDescription> findTypeForEntry (const juce::KnownPluginList& list,
                                                           const juce::String& fileOrId,
                                                           int uniqueId);

// Lässt sich eine gespeicherte Plugin-Kette vollständig aus der Fundliste aufbauen?
// Builtin-Konverter ("builtin:...") brauchen keinen Cache-Eintrag, alle anderen Einträge
// müssen auflösbar sein -- eine Kette nur teilweise aufzubauen würde die Reihenfolge
// verfälschen. Steuert, ob der Ketten-Restore sofort laufen kann oder auf Scan-Ergebnisse
// warten muss (AudioEngine::applyState / tryRestorePendingChain).
bool chainIsResolvable (const juce::Array<PluginEntryState>& plugins,
                        const juce::KnownPluginList& list);

// Liegt path in (oder unter) einem der Ordner? Echter Pfadvergleich statt String-Prefix
// (C:\Plugins darf nicht C:\Plugins2\... matchen); auf Windows case-insensitiv.
bool pathIsInsideAnyFolder (const juce::String& path, const juce::StringArray& folders);

// Maßgebliche Änderungszeit eines VST3-Pfads: für Einzeldateien die Datei-mtime; für
// Bundle-ORDNER die neueste mtime der inneren *.vst3-Binaries (Contents\<arch>-win\...).
// Grund: Companion-Dienste (UA Connect, Acustica Aquarius, Minimal Audio Stream, ...)
// schreiben Logs/Content in die Bundles und bumpen die Ordner-mtime zwischen den App-
// Starts -> Dauer-Rescan. Die Binary selbst ändert nur ein echtes Plugin-Update.
// Ordner ohne innere .vst3-Datei: Fallback auf die Ordner-mtime.
juce::Time effectiveModTime (const juce::File& vst3FileOrBundle);

// VST3-Format mit bundle-robustem Rescan-Check (siehe effectiveModTime). Überall dort
// verwenden, wo pluginNeedsRescanning/isListingUpToDate ins Spiel kommt.
#if JUCE_PLUGINHOST_VST3
struct MicVST3Format : juce::VST3PluginFormat
{
    bool pluginNeedsRescanning (const juce::PluginDescription& d) override
    {
        return effectiveModTime (juce::File (d.fileOrIdentifier)) != d.lastFileModTime;
    }
};
#endif

// Hintergrund-Thread um runScan herum; marshallt Callbacks auf den Message-Thread.
// Besitzt den echten ChildProcess-Runner (oder einen injizierten für Tests).
class ScanCoordinator : private juce::Thread
{
public:
    // Gemessen an WaveShell1-VST3 17.1 (347 Klassen, Waves "Essentials"): 178 s kalt,
    // ~90 s warm — JUCE instanziiert beim Scannen JEDE Klasse der Shell einzeln. Mit 120 s
    // lag das genau auf der Kippe: warm gerade noch drin, kalt Timeout -> "unresponsive",
    // und nach einem Factory-Reset war die Shell damit jedes Mal wieder weg. Größere
    // Waves-Installationen (mehr Klassen) brauchen entsprechend mehr. 600 s ist deshalb
    // bewusst reichlich; ein echt hängendes Plugin blockiert den Scan dadurch länger, was
    // aber verkraftbar ist: der Skip-Button bricht die aktuelle Datei ab, die Plugin-Kette
    // wartet nicht mehr auf das Scan-Ende und jede fertige Datei ist sofort gespeichert.
    static constexpr int defaultTimeoutMs = 600000;
    // "Retry skipped plugins" liegt gleichauf; sein Zweck ist inzwischen nicht mehr das
    // größere Zeitbudget, sondern das Übergehen der Skip-Liste (siehe retrySkippedPlugins).
    static constexpr int retryTimeoutMs   = 600000;

    // onFileScanned: Teilergebnis EINER Datei, sofort nach deren Scan (siehe runScan) --
    // hier übernimmt und persistiert der Aufrufer inkrementell. onFinished kommt nur, wenn
    // der Scan komplett durchlief, und trägt keine Daten mehr: die stecken alle schon in
    // den onFileScanned-Aufrufen, die ihm (FIFO auf dem Message-Thread) vorausgehen.
    ScanCoordinator (juce::StringArray filesToScan,
                     std::function<void (int, int, juce::String)> onProgress,
                     std::function<void (ScanOutcome)> onFileScanned,
                     std::function<void()> onFinished,
                     int timeoutMsIn = defaultTimeoutMs,
                     std::unique_ptr<ScanProcessRunner> runnerOverride = nullptr);
    ~ScanCoordinator() override;   // signalisiert Abbruch, killt laufenden Kindprozess

    void skipCurrentFile() { skipRequest.store (true); }   // vom Skip-Button (Message-Thread)

private:
    void run() override;

    juce::StringArray files;
    std::function<void (int, int, juce::String)> progressCb;
    std::function<void (ScanOutcome)> fileScannedCb;
    std::function<void()> finishedCb;
    int timeoutMs = defaultTimeoutMs;
    std::unique_ptr<ScanProcessRunner> runner;
    std::atomic<bool> skipRequest { false };

    // Lebenszeit-Wächter für bereits via callAsync eingereihte Callbacks: Destruktor
    // und die Lambdas laufen beide auf dem Message-Thread, daher ist ein simples bool
    // hinter shared_ptr racefrei; der Worker-Thread kopiert nur den shared_ptr selbst
    // (dessen Refcount threadsicher ist), fasst den bool aber nie an.
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
