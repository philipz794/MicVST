#include "audio/AudioEngine.h"
using IOProc = juce::AudioProcessorGraph::AudioGraphIOProcessor;

AudioEngine::AudioEngine()
{
    // WICHTIG: erzwingt das Erstellen + Scannen der Geräte-Typen. Ohne diesen Aufruf
    // ist availableDeviceTypes leer, getCurrentDeviceTypeObject() liefert nullptr und
    // setCurrentAudioDeviceType()/setAudioDeviceSetup() sowie die Device-Suche tun nichts.
    deviceManager.getAvailableDeviceTypes();
    deviceManager.addChangeListener (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    player.setProcessor (nullptr);
}

bool AudioEngine::isRunning() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    return dev != nullptr && dev->isPlaying();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Device kam/ging: AudioDeviceManager stellt das gespeicherte Setup selbst
    // wieder her (namensbasiert). Wir spiegeln nur den Status nach außen.
    juce::Logger::writeToLog (isRunning() ? "Audio: läuft"
                                          : "Audio: idle (Device getrennt?)");
    if (onStatusChanged) onStatusChanged();
    if (onDeviceChanged) onDeviceChanged();   // Geräte-Einstellungen persistieren
}

juce::String AudioEngine::detectCableOutput()
{
    // Render-Endpunkte bekannter virtueller Kabel, nach Priorität (VB-Cable zuerst).
    static const char* const cablePatterns[] = {
        "CABLE Input",          // VB-Audio Virtual Cable (empfohlen)
        "VB-Audio",             // weitere VB-Audio-Kabel (Hi-Fi Cable etc.)
        "VoiceMeeter Input",    // VoiceMeeter VAIO/Aux
        "Virtual Audio Cable"   // VAC ("Line 1 (Virtual Audio Cable)")
    };

    deviceManager.setCurrentAudioDeviceType (deviceManager.preferredTypeName(), true);
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        auto outs = type->getDeviceNames (false /* output */);
        for (auto* pat : cablePatterns)
            for (auto& name : outs)
                if (name.containsIgnoreCase (pat))
                    return name;
    }
    return {};
}

juce::String AudioEngine::initialise (const juce::String& inputDeviceName,
                                      const juce::String& outputDeviceName)
{
    deviceManager.removeAudioCallback (this);   // idempotent: doppelte Registrierung vermeiden
    deviceManager.closeAudioDevice();
    deviceManager.setCurrentAudioDeviceType (deviceManager.preferredTypeName(), true);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    // Namen VERBATIM übernehmen — auch leer. Ein leerer Output-Name bedeutet bewusst
    // "kein Host-Output" ("none"); der Input-WASAPI-Callback treibt die Engine dann allein.
    // Würden wir leer überspringen, bliebe das alte Default-Gerät stehen und "none"
    // ließe sich nicht speichern.
    setup.inputDeviceName  = inputDeviceName;
    setup.outputDeviceName = outputDeviceName;
    if (preferredBufferSize > 0)
        setup.bufferSize = preferredBufferSize;   // 0 = Auto: Geräte-Default nicht anfassen
    // Kanäle EXPLIZIT aktivieren. Auf useDefault* darf man sich nicht verlassen:
    // ohne deviceManager.initialise(numIn,numOut,...) ist numInputChansNeeded=0,
    // wodurch die "Default"-Input-Kanäle auf [0,0) = KEINE gesetzt würden.
    setup.useDefaultInputChannels  = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true);    // bis zu 2 Mic-Kanäle (mono nutzt nur ch0)
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);
    setup.sampleRate = 48000.0;   // bevorzugt; WASAPI shared kann die Mix-Rate des Geräts erzwingen

    // Fehler NICHT früh zurückgeben: Graph/Chain müssen immer existieren,
    // auch wenn (noch) kein Device offen ist (z. B. Gerät noch nicht da / Reconnect).
    const juce::String err = deviceManager.setAudioDeviceSetup (setup, true);

    if (auto* d = deviceManager.getCurrentAudioDevice())
        juce::Logger::writeToLog ("Setup: in=" + setup.inputDeviceName
            + " out=" + setup.outputDeviceName
            + " sr=" + juce::String (d->getCurrentSampleRate(), 0)
            + " buf=" + juce::String (d->getCurrentBufferSizeSamples()));
    else
        juce::Logger::writeToLog ("Setup: kein Device offen (" + err + ")");

    graph.clear();
    auto inNode  = graph.addNode (std::make_unique<IOProc> (IOProc::audioInputNode));
    auto outNode = graph.addNode (std::make_unique<IOProc> (IOProc::audioOutputNode));

    pluginChain = std::make_unique<PluginChain> (graph, inNode->nodeID, outNode->nodeID);
    rebuildGraph();   // leere Kette: in -> out (inkl. Mono→Stereo-Fanout)

    // Eigenen "spielenden" Playhead setzen, BEVOR der Player seinen (ohne isPlaying)
    // installiert. Der Player nutzt seinen nur, wenn der Graph keinen hat.
    if (auto* d = deviceManager.getCurrentAudioDevice())
        playHead.sampleRate.store (d->getCurrentSampleRate());
    graph.setPlayHead (&playHead);

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (this);
    return err;
}

void AudioEngine::setDeviceConfig (const juce::String& input, const juce::String& output,
                                  double sampleRate, int bufferSize)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.inputDeviceName  = input;
    setup.outputDeviceName = output;
    if (sampleRate > 0.0) setup.sampleRate = sampleRate;
    if (bufferSize > 0)   setup.bufferSize = bufferSize;
    // Kanäle EXPLIZIT (wie in initialise) — sonst droht 0 aktive Input-Kanäle.
    setup.useDefaultInputChannels  = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();  setup.inputChannels.setRange (0, 2, true);
    setup.outputChannels.clear(); setup.outputChannels.setRange (0, 2, true);

    deviceManager.setAudioDeviceSetup (setup, true);
    rebuildGraph();   // IO-Knoten-Kanalzahl kann sich geändert haben -> neu verdrahten
}

void AudioEngine::rebuildGraph()
{
    if (pluginChain != nullptr)
        pluginChain->rebuildConnections();
}

juce::File AudioEngine::pluginCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("MicVST").getChildFile ("plugin_cache.xml");
}

juce::StringArray AudioEngine::scanRoots() const
{
    // JUCE-Default-Orte statt hartkodiertem Pfad: deckt neben Program Files auch
    // %LOCALAPPDATA%\Programs\Common\VST3 und die VST3_PATH-Umgebungsvariable ab.
    MicVST3Format vst3;
    juce::StringArray roots;
    const auto defaults = vst3.getDefaultLocationsToSearch();
    for (int i = 0; i < defaults.getNumPaths(); ++i)
        roots.add (defaults[i].getFullPathName());
    for (auto& f : pluginFolders)
        if (f.isNotEmpty()) roots.add (f);
    return roots;
}

juce::StringArray AudioEngine::listVst3Files() const
{
    MicVST3Format vst3;
    juce::FileSearchPath paths;
    for (auto& r : scanRoots())
        paths.add (juce::File (r));
    paths.removeRedundantPaths();
    return vst3.searchPathsForPlugins (paths, true, true);
}

void AudioEngine::loadPluginCache()
{
    if (! PluginScanCache::load (pluginCacheFile(), knownPlugins, skippedPlugins))
        juce::Logger::writeToLog ("Plugin-Cache fehlt/korrupt -> voller Scan");
}

void AudioEngine::startBackgroundScan (int timeoutMs, const juce::StringArray& forceRescan)
{
    if (isScanning()) { rescanQueued = true; return; }

    MicVST3Format vst3;
    auto files = filterFilesNeedingScan (listVst3Files(), knownPlugins, vst3, skippedPlugins, forceRescan);
    juce::Logger::writeToLog ("Scan: " + juce::String (files.size()) + " Datei(en) zu scannen");
    if (files.isEmpty())
    {
        PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
        if (! pendingPlugins.isEmpty()) { restoreChain (pendingPlugins); pendingPlugins.clear(); }
        if (onScanFinished) onScanFinished();
        return;
    }

    scanner = std::make_unique<ScanCoordinator> (files,
        [this] (int cur, int total, juce::String name)
        {
            if (onScanProgress) onScanProgress (cur, total, name);
        },
        [this] (ScanOutcome delta) { applyScanDelta (delta); },
        [this] { handleScanFinished(); },
        timeoutMs);
}

// Teilergebnis EINER gescannten Datei übernehmen und sofort persistieren. Der Save pro
// Datei ist Absicht: bis v1.1.1 schrieb nur handleScanFinished den Cache, und das lief bei
// Abbruch (App-Quit mitten im Scan) nie -> die komplette Arbeit war weg und der nächste
// Start scannte alles erneut. Bei Nutzern mit vielen Plugins und Shell-Plugins (WaveShell,
// bis zu 120 s pro Datei) dauert ein Scan Minuten, ein Quit davor ist also der Normalfall
// und die Schleife schließt sich nie. Kosten: ein XML-Write pro Datei (JUCE schreibt via
// Temp-Datei + Rename, ein Crash kann den Cache also nicht halb beschrieben hinterlassen).
void AudioEngine::applyScanDelta (const ScanOutcome& delta)
{
    mergeScanResults (knownPlugins, delta);
    applySkipDelta (skippedPlugins, delta);
    for (auto& s : delta.skipped)
        juce::Logger::writeToLog ("Scan übersprungen (" + s.reason + "): " + s.file);
    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
    tryRestorePendingChain();
}

// Zurückgestellten Ketten-Restore erneut versuchen: sobald ALLE Plugins der gespeicherten
// Kette auflösbar sind, muss sie nicht mehr auf das Scan-ENDE warten. Vorher blieb die
// Mikrofon-Kette den ganzen (bei vielen Plugins minutenlangen) Scan über leer -- und wurde
// bei einem Abbruch mitten im Scan in dieser Sitzung überhaupt nicht wiederhergestellt.
void AudioEngine::tryRestorePendingChain()
{
    if (pendingPlugins.isEmpty() || ! chainIsResolvable (pendingPlugins, knownPlugins))
        return;

    juce::Logger::writeToLog ("Ketten-Restore: alle Plugins da, Kette wird aufgebaut");
    restoreChain (pendingPlugins);
    pendingPlugins.clear();
    if (onChainRestored) onChainRestored();   // UI-Zeilen neu aufbauen
}

void AudioEngine::handleScanFinished()
{
    scanner = nullptr;   // Callback kommt via callAsync -> wir sind auf dem Message-Thread

    // Die Ergebnisse stecken schon in knownPlugins/skippedPlugins: applyScanDelta lief für
    // jede Datei und ging diesem Callback auf dem Message-Thread voraus.

    // Ordner können während des Scans entfernt worden sein -> NACH dem Übernehmen wegputzen,
    // damit auch frisch gescannte Fremd-Ergebnisse rausfliegen.
    pruneOutsideFolders();

    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);

    // Letzte Chance für einen noch offenen Restore: hier OHNE Auflösbarkeits-Check, denn
    // nach dem Scan kommt nichts mehr nach -- fehlende Plugins werden einzeln geloggt und
    // übersprungen. Ein UI-Refresh braucht's nicht extra, onScanFinished macht rebuildRows.
    if (! pendingPlugins.isEmpty())
    {
        restoreChain (pendingPlugins);
        pendingPlugins.clear();
    }
    if (onScanFinished) onScanFinished();

    if (rescanQueued) { rescanQueued = false; startBackgroundScan(); }
}

void AudioEngine::rescanAllPlugins()
{
    if (isScanning()) return;
    knownPlugins.clear();
    skippedPlugins.clear();
    pluginCacheFile().deleteFile();
    startBackgroundScan();
}

void AudioEngine::retrySkippedPlugins()
{
    if (isScanning() || skippedPlugins.isEmpty()) return;

    // Alle (noch existierenden) geskippten Dateien erzwingen: forceRescan hebelt sowohl den
    // Skip-Filter als auch den Cache-Check aus -- letzteres, weil mergeScanResults gerettete
    // Typen sofort frisch stempelt und der Retry sonst stillschweigend nichts täte.
    // Die Skip-Liste wird NICHT vorab geleert: sonst stünde nach dem ersten Teilergebnis
    // eine leere Liste auf der Platte und ein Quit mitten im Retry würde die noch nicht
    // versuchten Einträge verlieren. applySkipDelta räumt pro Datei auf.
    juce::StringArray forceRescan;
    for (auto& s : skippedPlugins)
        if (juce::File (s.file).exists())
            forceRescan.add (s.file);

    startBackgroundScan (ScanCoordinator::retryTimeoutMs, forceRescan);
}

void AudioEngine::skipCurrentScanFile()
{
    if (scanner != nullptr) scanner->skipCurrentFile();
}

void AudioEngine::pruneOutsideFolders()
{
    const auto roots = scanRoots();
    for (auto& t : knownPlugins.getTypes())
        if (! pathIsInsideAnyFolder (t.fileOrIdentifier, roots)) knownPlugins.removeType (t);
    for (int i = skippedPlugins.size(); --i >= 0;)
        if (! pathIsInsideAnyFolder (skippedPlugins[i].file, roots)) skippedPlugins.remove (i);
}

void AudioEngine::addPluginFolder (const juce::String& folder)
{
    if (folder.isNotEmpty() && ! pluginFolders.contains (folder))
        pluginFolders.add (folder);
    startBackgroundScan();
}

void AudioEngine::removePluginFolder (const juce::String& folder)
{
    pluginFolders.removeString (folder);
    pruneOutsideFolders();
    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
    startBackgroundScan();
}

MicVSTState AudioEngine::captureState()
{
    MicVSTState s;
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    s.inputDevice  = setup.inputDeviceName;
    s.outputDevice = setup.outputDeviceName;
    s.sampleRate   = setup.sampleRate;
    s.bufferSize   = preferredBufferSize;
    s.pluginFolders = pluginFolders;

    // Ketten-Restore steht noch aus -> gemerkten Zustand verbatim zurückgeben,
    // sonst würde persistState() die gespeicherte Kette mit "leer" überschreiben.
    if (! pendingPlugins.isEmpty()) { s.plugins = pendingPlugins; return s; }
    if (pluginChain == nullptr) return s;

    for (auto& e : pluginChain->entries())
    {
        PluginEntryState p;
        p.fileOrId = e.fileOrId;
        p.bypassed = e.bypassed;
        p.uniqueId = e.uniqueId;   // nötig, um Plugins derselben Shell zu unterscheiden
        if (auto* node = graph.getNodeForId (e.node))
        {
            auto* proc = node->getProcessor();
            const juce::ScopedLock sl (proc->getCallbackLock());   // gegen Race mit processBlock
            proc->getStateInformation (p.state);
        }
        s.plugins.add (p);
    }
    return s;
}

void AudioEngine::applyState (const MicVSTState& s)
{
    setPreferredBufferSize (s.bufferSize);
    initialise (s.inputDevice, s.outputDevice);

    // Kette nur wiederherstellen, wenn alle Nicht-Builtin-Plugins im Cache auflösbar sind.
    // Sonst zurückstellen, bis der Scan sie geliefert hat (captureState gibt solange
    // pendingPlugins zurück, damit persistState die Kette nicht mit "leer" überschreibt).
    if (chainIsResolvable (s.plugins, knownPlugins))
    {
        restoreChain (s.plugins);
    }
    else
    {
        pendingPlugins = s.plugins;
        juce::Logger::writeToLog ("Ketten-Restore wartet auf Plugin-Scan");
    }
    rebuildGraph();
}

void AudioEngine::restoreChain (const juce::Array<PluginEntryState>& plugins)
{
    double sr = deviceManager.getCurrentAudioDevice() != nullptr
              ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 48000.0;

    for (auto& p : plugins)
    {
        if (p.fileOrId == PluginChain::monoToStereoId || p.fileOrId == PluginChain::stereoToMonoId)
        {
            if (p.fileOrId == PluginChain::monoToStereoId) pluginChain->addMonoToStereo();
            else                                           pluginChain->addStereoToMono();
            pluginChain->setBypass ((int) pluginChain->entries().size() - 1, p.bypassed);
            continue;
        }

        // NICHT getTypeForFile: bei Shell-Plugins teilen sich hunderte Typen eine Datei
        // (WaveShell), und getTypeForFile liefert immer den ersten -- die Kette bekäme
        // nach dem Neustart ein beliebiges Plugin der Shell. Siehe findTypeForEntry.
        auto type = findTypeForEntry (knownPlugins, p.fileOrId, p.uniqueId);
        if (type == nullptr) { juce::Logger::writeToLog ("Plugin fehlt: " + p.fileOrId); continue; }
        juce::String err;
        if (pluginChain->addPlugin (formatManager, *type, sr, 128, err))
        {
            const int idx = (int) pluginChain->entries().size() - 1;
            if (auto* node = graph.getNodeForId (pluginChain->entries()[(size_t) idx].node))
                node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
            pluginChain->setBypass (idx, p.bypassed);
        }
        else juce::Logger::writeToLog ("Plugin-Load: " + err);
    }
    rebuildGraph();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Geöffnete Geräte-/Kanalkonfiguration ins Log (hilft beim Diagnostizieren von Audio-Problemen).
    juce::Logger::writeToLog ("Device start: '" + device->getName() + "'"
        + " | inCh aktiv=" + juce::String (device->getActiveInputChannels().countNumberOfSetBits())
        + " von [" + device->getInputChannelNames().joinIntoString (", ") + "]"
        + " | outCh aktiv=" + juce::String (device->getActiveOutputChannels().countNumberOfSetBits())
        + " | sr=" + juce::String (device->getCurrentSampleRate(), 0)
        + " | buf=" + juce::String (device->getCurrentBufferSizeSamples()));
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    playHead.samples.fetch_add (numSamples, std::memory_order_relaxed);   // Transport voranschieben

    if (numInputChannels > 0)
    {
        juce::AudioBuffer<float> inView (const_cast<float* const*> (inputChannelData),
                                         numInputChannels, numSamples);
        inputMeter.process (inView);
    }

    // Graph verarbeiten (Input -> Kette -> Output).
    player.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                             outputChannelData, numOutputChannels,
                                             numSamples, context);

    if (numOutputChannels > 0)
    {
        juce::AudioBuffer<float> outView (outputChannelData, numOutputChannels, numSamples);
        outputMeter.process (outView);
    }
}
