#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
#include "audio/Vst3ClassScan.h"
#include "state/Persistence.h"
#include "state/AutostartRegistry.h"
#include "ui/MainComponent.h"
#include "ui/TrayIcon.h"

#if JUCE_WINDOWS
 // Bewusst kein <windows.h>: das Header pollutet nachfolgende JUCE-Header (Makrokonflikte
 // z.B. in juce_PushNotifications.h). Stattdessen nur die eine benötigte WinAPI-Funktion
 // per extern "C" deklarieren.
 extern "C" __declspec (dllimport) unsigned int __stdcall SetErrorMode (unsigned int);
 static constexpr unsigned int kSemFailCriticalErrors = 0x0001; // SEM_FAILCRITICALERRORS
 static constexpr unsigned int kSemNoGpFaultErrorBox  = 0x0002; // SEM_NOGPFAULTERRORBOX
 static constexpr unsigned int kSemNoOpenFileErrorBox  = 0x8000; // SEM_NOOPENFILEERRORBOX
 #include <excpt.h>   // SEH-Intrinsics (_exception_code) — bewusst ohne <windows.h>

// Enumeration mit SEH-Guard: Crasht eine Plugin-Klasse (WaveShell enthält hunderte),
// stirbt nicht mehr der ganze Prozess — die bis dahin gefundenen Typen bleiben
// nutzbar. Rückgabe 0 = sauber durchgelaufen, sonst der Exception-Code (NTSTATUS).
// In __try-Funktionen dürfen keine lokalen Objekte mit Destruktor liegen, daher nehmen
// beide Guards ihre Ausgabe per Referenz entgegen und rufen nur eine Funktion auf.
static unsigned int findTypesWithSehGuard (juce::VST3PluginFormat& format,
                                           juce::OwnedArray<juce::PluginDescription>& types,
                                           const juce::String& path)
{
    __try
    {
        format.findAllTypesForFile (types, path);
        return 0;
    }
    __except (1 /* EXCEPTION_EXECUTE_HANDLER */)
    {
        return (unsigned int) _exception_code();
    }
}

static unsigned int enumerateWithSehGuard (const juce::File& path,
                                           juce::Array<juce::PluginDescription>& out,
                                           juce::String& error,
                                           bool& okOut)
{
    __try
    {
        okOut = enumerateVst3Classes (path, out, error);
        return 0;
    }
    __except (1 /* EXCEPTION_EXECUTE_HANDLER */)
    {
        okOut = false;
        return (unsigned int) _exception_code();
    }
}
#endif

class MicVSTApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MicVST"; }
    const juce::String getApplicationVersion() override { return "1.2.0"; }

    // Kind-Scanprozesse (--scan) laufen parallel zur Haupt-Instanz und dürfen nicht
    // von der Single-Instance-Logik weggefangen werden.
    bool moreThanOneInstanceAllowed() override
    {
        return juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--scan");
    }

    // Kindmodus: genau EIN VST3 scannen und die Beschreibungen als XML in die --out-Datei
    // schreiben. Läuft in einem eigenen Prozess -> Hänger/Crashes können die App nie blockieren.
    // Rückgabe = Exit-Code (0 nur bei mindestens einem gefundenen Plugin-Typ).
    static int runScanChildMode()
    {
       #if JUCE_WINDOWS
        // Windows-Loader-Fehlerdialoge ("Ungültiges Bild", 0xC0000020) und Crash-Dialoge unterdrücken:
        // eine kaputte Plugin-DLL soll still als Fehler enden, nicht als Modal-Dialog.
        SetErrorMode (kSemFailCriticalErrors | kSemNoGpFaultErrorBox | kSemNoOpenFileErrorBox);
       #endif

        auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        juce::String pluginPath, outPath;
        for (int i = 0; i < args.size() - 1; ++i)
        {
            if (args[i] == "--scan") pluginPath = args[i + 1].unquoted();
            if (args[i] == "--out")  outPath    = args[i + 1].unquoted();
        }
        if (pluginPath.isEmpty() || outPath.isEmpty()) return 2;

        juce::OwnedArray<juce::PluginDescription> types;
        unsigned int crashCode = 0;

       #if JUCE_WINDOWS
        // Schneller Weg: Klassen nur über die Factory-Metadaten auflisten, ohne jede
        // einzelne zu instanziieren (siehe Vst3ClassScan.h). Bei Shell-Plugins ist das der
        // Unterschied zwischen ~1 s und Minuten — WaveShell1-VST3 17.1 (347 Klassen)
        // brauchte über JUCE 178 s und lief damit in den Scan-Timeout.
        {
            juce::Array<juce::PluginDescription> infoOnly;
            juce::String error;
            bool ok = false;
            crashCode = enumerateWithSehGuard (juce::File (pluginPath), infoOnly, error, ok);

            // Nicht auf ok prüfen, sondern auf das Ergebnis: crasht erst das Herunterfahren
            // der DLL, stehen die Klassen längst in infoOnly und sind voll brauchbar.
            juce::ignoreUnused (ok);
            for (auto& d : infoOnly)
                types.add (new juce::PluginDescription (d));
        }
       #endif

        // Fallback auf JUCE, wenn der schnelle Weg nichts geliefert hat: deckt Bundles mit
        // moduleinfo.json, ungewohnte Layouts und alles ab, was unser schlanker Loader
        // nicht kann. Ein Crash im schnellen Weg wird dabei bewusst NICHT wiederholt.
        juce::VST3PluginFormat format;
        if (types.isEmpty() && crashCode == 0)
        {
           #if JUCE_WINDOWS
            crashCode = findTypesWithSehGuard (format, types, pluginPath);
           #else
            format.findAllTypesForFile (types, pluginPath);
           #endif
        }

        if (crashCode == 0 && types.isEmpty()) return 1;

        juce::XmlElement root ("MicVSTScanResult");
        if (crashCode != 0)
            root.setAttribute ("crashCode",
                               "0x" + juce::String::toHexString ((int) crashCode).toUpperCase());
        for (auto* t : types)
            root.addChildElement (t->createXml().release());
        if (! root.writeTo (juce::File (outPath))) return 1;
        return crashCode != 0 ? 3 : 0;
    }

    void initialise (const juce::String& commandLine) override
    {
        if (juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--scan"))
        {
            setApplicationReturnValue (runScanChildMode());
            quit();
            return;
        }

        auto logFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("MicVST").getChildFile ("log.txt");
        logFile.getParentDirectory().createDirectory();
        logger.reset (new juce::FileLogger (logFile, "MicVST Log"));
        juce::Logger::setCurrentLogger (logger.get());

        const bool silent = commandLine.contains ("--tray");

        engine = std::make_unique<AudioEngine>();

        // Gespeicherten Zustand laden; NUR beim allerersten Start (noch keine config.xml)
        // Devices vorbelegen. Sobald eine Config existiert, gilt sie verbatim — sonst würde
        // eine bewusst auf "none" gesetzte Output-Auswahl beim Neustart wieder überschrieben.
        const bool firstRun = ! configFile().existsAsFile();
        MicVSTState state = loadState();

        // Auto-Update-Check-Zustand übernehmen (Default: aus, nie gefragt).
        updateCheckEnabled  = state.updateCheckEnabled;
        updateCheckAsked    = state.updateCheckAsked;
        lastNotifiedVersion = state.lastNotifiedVersion;

        // Custom-VST3-Ordner übernehmen, dann Cache laden (vor applyState und vor der UI).
        engine->setPluginFolders (state.pluginFolders);
        engine->loadPluginCache();   // KEIN synchroner Scan mehr — Cache reicht zum Start
        if (firstRun)
        {
            // Output bevorzugt ein virtuelles Kabel (VB-Cable & Co): die App liefert dann
            // direkt an ein virtuelles Mikro, ohne zusätzliches Ausgabe-Plugin. Kein Kabel
            // gefunden -> Output bleibt leer ("none"), die UI weist auf den Download hin.
            state.outputDevice = engine->detectCableOutput();
            juce::Logger::writeToLog (state.outputDevice.isEmpty()
                ? "No virtual cable found -> output 'none'"
                : "Host output (cable) = " + state.outputDevice);

            // Input: System-Standardmikrofon, sonst erstes verfügbares Eingangsgerät.
            auto& dm = engine->getDeviceManager();
            dm.setCurrentAudioDeviceType (dm.preferredTypeName(), true);
            if (auto* type = dm.getCurrentDeviceTypeObject())
            {
                type->scanForDevices();
                auto ins = type->getDeviceNames (true);
                const int def = type->getDefaultDeviceIndex (true);
                state.inputDevice = juce::isPositiveAndBelow (def, ins.size()) ? ins[def]
                                  : (ins.isEmpty() ? juce::String() : ins[0]);
            }
            juce::Logger::writeToLog ("Input = " + state.inputDevice);
        }
        engine->applyState (state);

        mainWindow = std::make_unique<MainWindow> ("MicVST", *engine, state.windowState);
        mainWindow->setVisible (! silent);   // Autostart (--tray): unsichtbar, nur Tray

        tray = std::make_unique<TrayIcon>();
        tray->onToggleWindow = [this] { toggleWindow(); };
        tray->onQuit         = [this] { systemRequestedQuit(); };
        tray->isAutostartOn  = [] { return AutostartRegistry::isEnabled(); };
        tray->setAutostart   = [] (bool on) { AutostartRegistry::setEnabled (on); };

        // Beim Verstecken: einmaligen Tray-Hinweis zeigen und die aktuelle Fenstergröße sichern
        // (X beendet nicht, daher hier persistieren, damit eine geänderte Größe nicht verloren geht).
        mainWindow->onHide = [this] { maybeShowTrayHint(); persistState(); };

        // Erst JETZT (nach applyState) das Persistieren bei Geräte-Änderungen aktivieren,
        // damit In-/Output-Auswahl gemerkt wird — auch ohne sauberes Beenden (X versteckt nur).
        engine->onDeviceChanged = [this] { persistState(); };
        // UI-Änderungen (Plugin-Kette, Ordner) persistieren ebenfalls den Gesamtzustand —
        // ein direktes saveState(captureState()) in der UI würde windowState/Update-Check resetten.
        engine->onStateChanged  = [this] { persistState(); };

        engine->onFactoryResetRequested = [this] { factoryReset(); };

        // --- Auto-Update-Check verdrahten ---
        if (auto* mc = mainWindow->getContent())
        {
            mc->onUpdateCheckToggled = [this] (bool on) { updateCheckEnabled = on; persistState(); };
            mc->onUpdateFound = [this] (const juce::String& latestVersion, const juce::String&)
            {
                // Tray-Bubble nur EINMAL pro neuer Version (der Fenster-Hinweis bleibt sowieso).
                if (latestVersion == lastNotifiedVersion) return;
                lastNotifiedVersion = latestVersion;
                persistState();
                if (tray != nullptr)
                    tray->showInfoBubble ("Update available",
                        "MicVST " + latestVersion + " is available - click the version number to update.");
            };
            // Bei aktivem Check sofort einen Lauf starten (auch im stillen Autostart -> Tray-Bubble).
            mc->setUpdateCheckEnabled (updateCheckEnabled, true);
        }

        // Altes Dead-Man's-Pedal aufräumen (ersetzt durch Skip-Liste im plugin_cache.xml).
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MicVST").getChildFile ("plugin_scan.tmp").deleteFile();

        // Fenster + Tray stehen -> jetzt (und erst jetzt) im Hintergrund scannen.
        engine->startBackgroundScan();

        // Erststart: einmalig nach Zustimmung fragen (nur mit sichtbarem Fenster).
        if (! silent && ! updateCheckAsked)
            askUpdateConsent();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr) { mainWindow->setVisible (true); mainWindow->toFront (true); }
    }

    void systemRequestedQuit() override
    {
        persistState();
        quit();
    }

    void shutdown() override
    {
        tray = nullptr;
        mainWindow = nullptr;
        engine = nullptr;
        juce::Logger::setCurrentLogger (nullptr);
        logger = nullptr;
    }

private:
    // Engine-Zustand (Geräte/Plugins) + Fenstergröße/-position + Update-Check-Zustand speichern.
    void persistState()
    {
        if (suppressPersist || engine == nullptr) return;
        auto s = engine->captureState();
        if (mainWindow != nullptr) s.windowState = mainWindow->getWindowStateAsString();
        s.updateCheckEnabled  = updateCheckEnabled;
        s.updateCheckAsked    = updateCheckAsked;
        s.lastNotifiedVersion = lastNotifiedVersion;
        saveState (s);
    }

    // "Reset app (clear all data)": Einstellungen + Plugin-Cache + Tray-Hinweis-Marker
    // löschen und die App beenden. Kein Auto-Relaunch (Single-Instance-Logik würde die
    // neue Instanz blocken); der Dialog kündigt den manuellen Neustart an. Der
    // Autostart-Registry-Eintrag bleibt bewusst erhalten (sichtbare, eigene Einstellung).
    void factoryReset()
    {
        suppressPersist = true;
        configFile().deleteFile();
        AudioEngine::pluginCacheFile().deleteFile();
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MicVST").getChildFile ("tray_hint_shown").deleteFile();
        juce::Logger::writeToLog ("Factory-Reset: Daten gelöscht, App beendet sich");
        systemRequestedQuit();
    }

    // Einmaliges Erststart-Popup: Zustimmung zum Update-Check einholen. Egal wie der Nutzer
    // entscheidet -> "gefragt" wird gemerkt, also kommt der Dialog nie wieder.
    void askUpdateConsent()
    {
        updateCheckAsked = true;
        persistState();
        juce::NativeMessageBox::showYesNoBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Check for updates?",
            "Should MicVST check GitHub for a newer version on startup?\n\n"
            "Only a single request is sent to GitHub - no data is collected and there is no "
            "auto-installer. You can change this anytime with the \"Auto-Update-Check\" box.",
            nullptr,
            juce::ModalCallbackFunction::create ([this] (int result)
            {
                const bool yes = (result == 1);
                updateCheckEnabled = yes;
                persistState();
                if (mainWindow != nullptr)
                    if (auto* mc = mainWindow->getContent())
                        mc->setUpdateCheckEnabled (yes, true);   // bei Ja: gleich prüfen
            }));
    }

    void toggleWindow()
    {
        if (mainWindow == nullptr) return;
        const bool show = ! mainWindow->isVisible();
        mainWindow->setVisible (show);
        if (show) mainWindow->toFront (true);
        else      maybeShowTrayHint();
    }

    // Einmaliger Tray-Hinweis (Marker in %APPDATA%\MicVST), damit Nutzer nicht denken,
    // sie hätten die App beendet, wenn das Fenster verschwindet.
    void maybeShowTrayHint()
    {
        if (tray == nullptr) return;
        auto marker = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("MicVST").getChildFile ("tray_hint_shown");
        if (marker.existsAsFile()) return;
        marker.create();
        tray->showInfoBubble ("MicVST", "Still running in the tray - right-click the icon for options.");
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, AudioEngine& engine, const juce::String& windowState)
            : DocumentWindow (name, juce::Colours::darkgrey, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            // resize-to-fit AUS: die Fenstergröße diktiert centreWithSize unten, nicht die
            // Content-Größe. Sonst zieht der MainComponent (setSize im Ctor) die Startgröße
            // über das Minimum hinaus.
            content = new MainComponent (engine);
            setContentOwned (content, false);
            setResizable (true, false);
            // Mindestgröße = Startgröße (600x480); zusätzliche Höhe verlängert die Plugin-Liste.
            setResizeLimits (600, 480, 1600, 1400);
            // Letzte Größe/Position wiederherstellen (innerhalb der Resize-Limits), sonst zentriert
            // auf Startgröße. restoreWindowStateFromString respektiert die gesetzten Limits.
            if (windowState.isNotEmpty())
                restoreWindowStateFromString (windowState);
            else
                centreWithSize (600, 480);
        }
        std::function<void()> onHide;
        void closeButtonPressed() override { setVisible (false); if (onHide) onHide(); }
        MainComponent* getContent() const { return content; }
    private:
        MainComponent* content = nullptr;   // gehört dem Fenster (setContentOwned), nur Zugriffszeiger
    };

    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> tray;

    // Auto-Update-Check-Zustand (in config.xml persistiert, siehe persistState()).
    bool updateCheckEnabled = false;
    bool updateCheckAsked   = false;
    juce::String lastNotifiedVersion;
    bool suppressPersist = false;   // Factory-Reset: nichts darf die gelöschte Config neu anlegen
};

START_JUCE_APPLICATION (MicVSTApplication)
