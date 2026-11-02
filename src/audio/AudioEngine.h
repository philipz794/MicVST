#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>   // AudioProcessorPlayer lebt in juce_audio_utils
#include "audio/Metering.h"
#include "audio/PluginChain.h"
#include "audio/MicVSTDeviceManager.h"
#include "audio/ScanCoordinator.h"
#include "state/Persistence.h"

// Besitzt AudioDeviceManager + AudioProcessorGraph. Der Graph läuft über einen
// internen AudioProcessorPlayer; AudioEngine bleibt der Device-Callback und
// metert Input/Output rund um den Player herum.
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::ChangeListener
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::String initialise (const juce::String& inputDeviceName,
                             const juce::String& outputDeviceName);

    // Laufzeit-Geräteumschaltung (vom DevicePanel): setzt Geräte/Samplerate/Buffer neu,
    // OHNE Graph + Plugin-Kette neu aufzubauen. sampleRate<=0 / bufferSize<=0 = unverändert.
    void setDeviceConfig (const juce::String& input, const juce::String& output,
                          double sampleRate, int bufferSize);

    // Buffer-Wunsch des Users in Samples; 0 = Auto (Geräte-Default-Periode).
    // Wird von applyState gesetzt und in captureState persistiert.
    void setPreferredBufferSize (int samples) { preferredBufferSize = samples; }
    int  getPreferredBufferSize() const       { return preferredBufferSize; }

    // Sucht ein installiertes virtuelles Audio-Kabel als Output (VB-Cable, VoiceMeeter, VAC),
    // die Render->Capture selbst spiegeln. Leerer String = kein Kabel gefunden.
    juce::String detectCableOutput();

    MicVSTState captureState();            // liest Devices + Plugin-Kette + Blobs
    void          applyState (const MicVSTState&);   // lädt Devices + Plugins + setStateInformation

    bool isRunning() const;                 // true wenn ein Audio-Device offen ist und spielt
    std::function<void()> onStatusChanged;  // wird bei Device-Änderungen aufgerufen (UI-Status)
    std::function<void()> onDeviceChanged;  // wird bei Geräte-Änderungen aufgerufen (zum Persistieren)

    // Von der UI bei Ketten-/Ordner-Änderungen gerufen. Die App persistiert dann den
    // GESAMTEN Zustand (inkl. windowState + Update-Check-Feldern, die captureState nicht
    // kennt) — ein direktes saveState(captureState()) würde diese Felder zurücksetzen.
    std::function<void()> onStateChanged;
    void requestPersist() { if (onStateChanged) onStateChanged(); }

    // Factory-Reset (UI-Menü): Die App löscht config + Plugin-Cache und beendet sich
    // für einen frischen Erststart. Engine-seitig nur der Durchreich-Callback.
    std::function<void()> onFactoryResetRequested;
    void requestFactoryReset() { if (onFactoryResetRequested) onFactoryResetRequested(); }

    MicVSTDeviceManager&            getDeviceManager() { return deviceManager; }
    juce::AudioProcessorGraph&      getGraph()         { return graph; }
    PluginChain&                    getChain()         { return *pluginChain; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList&          getKnownPlugins()  { return knownPlugins; }

    // --- Plugin-Scan (out-of-process, asynchron, gecacht) ---
    void loadPluginCache();                    // beim Start VOR applyState aufrufen
    // forceRescan: siehe filterFilesNeedingScan -- Dateien, die trotz aktuell aussehendem
    // Cache erneut versucht werden sollen (retrySkippedPlugins nach Crash-Rescue).
    void startBackgroundScan (int timeoutMs = ScanCoordinator::defaultTimeoutMs,
                              const juce::StringArray& forceRescan = {});
    void rescanAllPlugins();                   // Cache + Skip-Liste leeren, alles neu
    void retrySkippedPlugins();                // nur Skip-Liste leeren, mit großem Timeout scannen
    void skipCurrentScanFile();                // Skip-Button: aktuelle Datei überspringen
    bool isScanning() const { return scanner != nullptr; }
    const juce::Array<SkippedPlugin>& getSkippedPlugins() const { return skippedPlugins; }
    std::function<void (int, int, juce::String)> onScanProgress;   // current(1-based), total, name
    std::function<void()> onScanFinished;      // nach Cache-Save + ggf. Ketten-Restore
    // Ein zurückgestellter Ketten-Restore ist MITTEN im Scan gelaufen (alle Plugins der
    // Kette sind da, das Scan-Ende steht aber noch aus) -> die UI muss ihre Zeilen neu
    // aufbauen, ohne dass der Scan-Status sich ändert.
    std::function<void()> onChainRestored;

    void addPluginFolder (const juce::String& folder);
    void removePluginFolder (const juce::String& folder);
    void setPluginFolders (const juce::StringArray& f) { pluginFolders = f; }
    const juce::StringArray& getPluginFolders() const  { return pluginFolders; }

    static juce::File pluginCacheFile();

    LevelReading inputLevel()  const { return inputMeter.read(); }
    LevelReading outputLevel() const { return outputMeter.read(); }

    void rebuildGraph();   // Graph-Verbindungen neu aufbauen (inkl. Mono->Stereo-Fanout)

private:
    // Playhead, der dem Graph (und damit allen Plugins) durchgehend "Transport läuft"
    // meldet. Nötig, weil der Default-Playhead des AudioProcessorPlayer isPlaying NICHT
    // setzt — manche Routing/Streaming-Plugins senden aber nur bei laufendem Transport.
    struct PlayingHead : juce::AudioPlayHead
    {
        std::atomic<juce::int64> samples { 0 };
        std::atomic<double> sampleRate { 48000.0 };
        juce::Optional<PositionInfo> getPosition() const override
        {
            const auto s  = samples.load (std::memory_order_relaxed);
            const auto sr = sampleRate.load (std::memory_order_relaxed);
            PositionInfo info;
            info.setIsPlaying (true);
            info.setIsRecording (false);
            info.setIsLooping (false);
            info.setTimeInSamples (s);
            info.setTimeInSeconds ((double) s / sr);
            info.setBpm (120.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature{});
            info.setPpqPosition (((double) s / sr) * (120.0 / 60.0));
            return info;
        }
    };
    PlayingHead playHead;

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    MicVSTDeviceManager deviceManager;   // WASAPI Low-Latency bevorzugt, Shared als Fallback (siehe MicVSTDeviceManager)
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<PluginChain> pluginChain;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::StringArray pluginFolders;   // zusätzliche VST3-Suchordner (persistiert)
    int preferredBufferSize = 0;   // Buffer-Wunsch des Users in Samples; 0 = Auto
    LevelMeter inputMeter, outputMeter;

    std::unique_ptr<ScanCoordinator> scanner;      // != nullptr solange ein Scan läuft
    bool rescanQueued = false;   // merkt einen während des Scans angeforderten Folgescan vor
    juce::Array<SkippedPlugin> skippedPlugins;     // persistiert im Cache
    juce::Array<PluginEntryState> pendingPlugins;  // Ketten-Restore wartet auf Scan-Ende
    juce::StringArray scanRoots() const;           // JUCE-Default-VST3-Orte + Custom-Ordner
    juce::StringArray listVst3Files() const;       // Standard- + Custom-Ordner enumerieren
    void restoreChain (const juce::Array<PluginEntryState>& plugins);
    void applyScanDelta (const ScanOutcome& delta);   // Ergebnis EINER Datei übernehmen + speichern
    void tryRestorePendingChain();                    // Restore sobald alle Ketten-Plugins da sind
    void handleScanFinished();                        // nur noch Prune/Restore, Daten sind schon drin
    void pruneOutsideFolders();                    // Cache-Einträge entfernter Ordner löschen
};
