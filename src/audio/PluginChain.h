#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/GraphConnections.h"
#include <vector>

// Verwaltet die geordnete VST3-Kette innerhalb eines AudioProcessorGraph.
// Hält pro Plugin einen Node + Bypass-Flag. Baut bei jeder Änderung die
// Verbindungen input->...->output neu auf (via computeChainConnections).
class PluginChain
{
public:
    // Marker im fileOrId-Feld für die internen Glieder (kein VST).
    static constexpr const char* monoToStereoId = "builtin:mono2stereo";
    static constexpr const char* stereoToMonoId = "builtin:stereo2mono";

    // uniqueId: unterscheidet Plugins, die sich eine Datei teilen (Shell-Plugins wie
    // WaveShell bringen hunderte Typen hinter EINEM Pfad mit). 0 = internes Glied bzw.
    // aus einer Config vor Einführung des Feldes geladen.
    struct Entry { NodeID node; juce::String fileOrId; bool bypassed = false; int uniqueId = 0;
                   bool isBuiltIn() const { return fileOrId.startsWith ("builtin:"); } };

    PluginChain (juce::AudioProcessorGraph& graph,
                 NodeID inputNode, NodeID outputNode);

    // Lädt ein VST3 (synchron) und hängt es ans Ende der Kette. Gibt false bei Fehler.
    bool addPlugin (juce::AudioPluginFormatManager& fm,
                    const juce::PluginDescription& desc,
                    double sampleRate, int blockSize,
                    juce::String& errorOut);

    // Hängt ein internes Glied ans Ende der Kette: Mono->Stereo (1 in -> 2 out, dupliziert)
    // bzw. Stereo->Mono (2 in -> 1 out, gemittelt).
    void addMonoToStereo();
    void addStereoToMono();

    void removePlugin (int index);
    void movePlugin (int from, int to);
    void setBypass (int index, bool shouldBypass);

    const std::vector<Entry>& entries() const { return chain; }

    // Baut alle Graph-Verbindungen neu auf. Pro Übergang werden min(quelle.outs, ziel.ins)
    // Kanäle verbunden -> wechselnde Kanalbreite (z. B. ein Mono->Stereo-Glied) wird
    // automatisch korrekt verdrahtet.
    void rebuildConnections();

private:
    juce::AudioProcessorGraph& graph;
    NodeID inputNode, outputNode;
    std::vector<Entry> chain;
};
