#include "audio/PluginChain.h"

namespace
{
    // Internes Glied: 1 Eingangskanal -> 2 Ausgangskanäle (dupliziert). So kann die
    // Kette davor mono laufen (z. B. rnnoise mono = halbe Last) und ab hier stereo.
    class MonoToStereoProcessor : public juce::AudioProcessor
    {
    public:
        MonoToStereoProcessor()
            : juce::AudioProcessor (BusesProperties()
                  .withInput  ("In",  juce::AudioChannelSet::mono(),   true)
                  .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}

        const juce::String getName() const override { return "Mono -> Stereo"; }

        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (buffer.getNumChannels() >= 2)
                buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());   // ch1 = ch0
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override  { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }

        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    // Internes Glied: 2 Eingangskanäle -> 1 Ausgangskanal (gemittelt L+R).
    class StereoToMonoProcessor : public juce::AudioProcessor
    {
    public:
        StereoToMonoProcessor()
            : juce::AudioProcessor (BusesProperties()
                  .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
                  .withOutput ("Out", juce::AudioChannelSet::mono(),   true)) {}

        const juce::String getName() const override { return "Stereo -> Mono"; }

        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            if (buffer.getNumChannels() >= 2)
            {
                auto* mono = buffer.getWritePointer (0);
                const auto* r = buffer.getReadPointer (1);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    mono[i] = 0.5f * (mono[i] + r[i]);   // ch0 = (L + R) / 2
            }
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override  { return false; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }

        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };
}

PluginChain::PluginChain (juce::AudioProcessorGraph& g, NodeID in, NodeID out)
    : graph (g), inputNode (in), outputNode (out) {}

bool PluginChain::addPlugin (juce::AudioPluginFormatManager& fm,
                             const juce::PluginDescription& desc,
                             double sampleRate, int blockSize,
                             juce::String& errorOut)
{
    if (fm.getNumFormats() == 0)
        juce::addDefaultFormatsToManager (fm);   // JUCE 8.0.13: addDefaultFormats() ist =delete

    auto instance = fm.createPluginInstance (desc, sampleRate, blockSize, errorOut);
    if (instance == nullptr) return false;

    // NICHT enableAllBuses() (kann Aux-/Multi-Busse aktivieren und das Send-Routing
    // mancher Plugins verbiegen — OBS/Wave Link machen das auch nicht). Nur falls das
    // Plugin ohne aktive Main-Busse lädt, den Main-In/Out aktivieren.
    if (instance->getTotalNumInputChannels() == 0)
        if (auto* b = instance->getBus (true, 0))  b->enable (true);
    if (instance->getTotalNumOutputChannels() == 0)
        if (auto* b = instance->getBus (false, 0)) b->enable (true);

    auto node = graph.addNode (std::move (instance));
    if (node == nullptr) { errorOut = "addNode failed"; return false; }

    chain.push_back ({ node->nodeID, desc.fileOrIdentifier, false, desc.uniqueId });
    return true;
}

void PluginChain::addMonoToStereo()
{
    auto node = graph.addNode (std::make_unique<MonoToStereoProcessor>());
    if (node == nullptr) return;
    chain.push_back ({ node->nodeID, monoToStereoId, false });
}

void PluginChain::addStereoToMono()
{
    auto node = graph.addNode (std::make_unique<StereoToMonoProcessor>());
    if (node == nullptr) return;
    chain.push_back ({ node->nodeID, stereoToMonoId, false });
}

void PluginChain::removePlugin (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) chain.size())) return;
    graph.removeNode (chain[(size_t) index].node);
    chain.erase (chain.begin() + index);
}

void PluginChain::movePlugin (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, (int) chain.size())) return;
    to = juce::jlimit (0, (int) chain.size() - 1, to);
    auto e = chain[(size_t) from];
    chain.erase (chain.begin() + from);
    chain.insert (chain.begin() + to, e);
}

void PluginChain::setBypass (int index, bool b)
{
    if (! juce::isPositiveAndBelow (index, (int) chain.size())) return;
    chain[(size_t) index].bypassed = b;
    if (auto* node = graph.getNodeForId (chain[(size_t) index].node))
        node->setBypassed (b);
}

void PluginChain::rebuildConnections()
{
    // Alle bestehenden Verbindungen lösen, dann kanalbewusst neu setzen.
    for (auto& c : graph.getConnections())
        graph.removeConnection (c);

    auto channelsOf = [this] (NodeID id) -> NodeChannels
    {
        if (auto* n = graph.getNodeForId (id))
            return { id, n->getProcessor()->getTotalNumInputChannels(),
                         n->getProcessor()->getTotalNumOutputChannels() };
        return { id, 0, 0 };
    };

    std::vector<NodeChannels> seq;
    seq.push_back (channelsOf (inputNode));
    for (auto& e : chain)
        if (! e.bypassed) seq.push_back (channelsOf (e.node));   // gebypasste Glieder überspringen
    seq.push_back (channelsOf (outputNode));

    for (auto& cc : computeChainConnections (seq))
        graph.addConnection ({ { cc.sourceNode, cc.sourceChannel },
                               { cc.destNode,   cc.destChannel } });
}
