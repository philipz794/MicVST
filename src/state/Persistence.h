#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_core/juce_core.h>

struct PluginEntryState
{
    juce::String fileOrId;
    bool bypassed = false;
    juce::MemoryBlock state;   // getStateInformation()-Blob
    // Unterscheidet Plugins, die sich eine Datei teilen (Shell-Plugins wie WaveShell:
    // hunderte Typen hinter EINEM Pfad). 0 = internes Glied oder Config aus einer Version
    // vor Einführung des Feldes -> findTypeForEntry nimmt dann den ersten Treffer der Datei.
    int uniqueId = 0;
};

struct MicVSTState
{
    juce::String inputDevice, outputDevice;
    double sampleRate = 48000.0;
    int    bufferSize = 0;   // Buffer-Wunsch in Samples; 0 = Auto (Geräte-Default)
    juce::Array<PluginEntryState> plugins;
    juce::StringArray pluginFolders;   // zusätzliche VST3-Suchordner
    juce::String windowState;          // DocumentWindow::getWindowStateAsString() (Größe/Position)

    // Opt-in Auto-Update-Check (siehe UpdateChecker). Default: aus, nie gefragt.
    bool updateCheckEnabled = false;   // Checkbox-Zustand
    bool updateCheckAsked   = false;   // Erststart-Popup schon gezeigt?
    juce::String lastNotifiedVersion;  // letzte per Tray-Bubble gemeldete Version (Dedup)
};

juce::ValueTree  toValueTree (const MicVSTState&);
MicVSTState    fromValueTree (const juce::ValueTree&);

// Datei unter %APPDATA%\MicVST\config.xml
juce::File    configFile();
bool          saveState (const MicVSTState&);
MicVSTState loadState();
