#include <juce_core/juce_core.h>
#include <iostream>
#include "audio/Metering.h"

// Smoke-Test: beweist, dass der UnitTestRunner läuft.
struct SmokeTest : juce::UnitTest
{
    SmokeTest() : juce::UnitTest ("Smoke") {}
    void runTest() override
    {
        beginTest ("runner works");
        expect (1 + 1 == 2);
    }
};
static SmokeTest smokeTest;

struct MeteringTest : juce::UnitTest
{
    MeteringTest() : juce::UnitTest ("Metering") {}
    void runTest() override
    {
        beginTest ("silence is zero");
        {
            juce::AudioBuffer<float> buf (2, 256);
            buf.clear();
            auto r = computeLevel (buf);
            expectWithinAbsoluteError (r.rms,  0.0f, 1.0e-6f);
            expectWithinAbsoluteError (r.peak, 0.0f, 1.0e-6f);
        }

        beginTest ("DC at 1.0 -> rms 1, peak 1");
        {
            juce::AudioBuffer<float> buf (1, 128);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (0, i, 1.0f);
            auto r = computeLevel (buf);
            expectWithinAbsoluteError (r.rms,  1.0f, 1.0e-4f);
            expectWithinAbsoluteError (r.peak, 1.0f, 1.0e-4f);
        }

        beginTest ("peak picks max abs across channels");
        {
            juce::AudioBuffer<float> buf (2, 64);
            buf.clear();
            buf.setSample (0, 10,  0.25f);
            buf.setSample (1, 20, -0.80f);
            auto r = computeLevel (buf);
            expectWithinAbsoluteError (r.peak, 0.80f, 1.0e-4f);
        }
    }
};
static MeteringTest meteringTest;

#include "audio/GraphConnections.h"

struct GraphConnectionsTest : juce::UnitTest
{
    GraphConnectionsTest() : juce::UnitTest ("GraphConnections") {}
    void runTest() override
    {
        const NodeID in   { 1 };
        const NodeID out  { 2 };
        const NodeID p1   { 10 };
        const NodeID p2   { 11 };

        beginTest ("empty mono chain: input(1out) -> output(2in) = 1 Kanal");
        {
            auto c = computeChainConnections ({ { in, 0, 1 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 1);
            expect (c[0] == ChannelConnection { in, 0, out, 0 });
        }

        beginTest ("empty stereo chain: input(2out) -> output(2in) = 2 Kanäle");
        {
            auto c = computeChainConnections ({ { in, 0, 2 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 2);
            expect (c[0] == ChannelConnection { in, 0, out, 0 });
            expect (c[1] == ChannelConnection { in, 1, out, 1 });
        }

        beginTest ("mono plugin then mono->stereo converter then stereo out");
        {
            // input(1) -> p1 mono(1/1) -> p2 mono->stereo(1/2) -> output(2)
            auto c = computeChainConnections ({ { in, 0, 1 }, { p1, 1, 1 },
                                                { p2, 1, 2 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 4);
            expect (c[0] == ChannelConnection { in, 0, p1, 0 });   // input->p1 (1 Kanal)
            expect (c[1] == ChannelConnection { p1, 0, p2, 0 });   // p1->converter (1 Kanal)
            expect (c[2] == ChannelConnection { p2, 0, out, 0 });  // converter->out (2 Kanäle)
            expect (c[3] == ChannelConnection { p2, 1, out, 1 });
        }

        beginTest ("two stereo plugins: in(2)->p1(2/2)->p2(2/2)->out(2)");
        {
            auto c = computeChainConnections ({ { in, 0, 2 }, { p1, 2, 2 },
                                                { p2, 2, 2 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 6);
            expect (c[0] == ChannelConnection { in, 0, p1, 0 });
            expect (c[1] == ChannelConnection { in, 1, p1, 1 });
            expect (c[2] == ChannelConnection { p1, 0, p2, 0 });
            expect (c[3] == ChannelConnection { p1, 1, p2, 1 });
            expect (c[4] == ChannelConnection { p2, 0, out, 0 });
            expect (c[5] == ChannelConnection { p2, 1, out, 1 });
        }
    }
};
static GraphConnectionsTest graphConnectionsTest;

#include "state/Persistence.h"

struct PersistenceTest : juce::UnitTest
{
    PersistenceTest() : juce::UnitTest ("Persistence") {}
    void runTest() override
    {
        beginTest ("round-trip preserves fields + plugin blob");
        MicVSTState s;
        s.inputDevice = "Microphone"; s.outputDevice = "CABLE Output";
        s.sampleRate = 48000.0; s.bufferSize = 128;
        s.pluginFolders.add ("C:/My VST3");
        s.windowState = "0 0 600 480";
        s.updateCheckEnabled = true;
        s.updateCheckAsked = true;
        s.lastNotifiedVersion = "1.0.3";

        PluginEntryState p;
        p.fileOrId = "C:/VST3/Pro-Q 3.vst3";
        p.bypassed = true;
        const char raw[] = { 1, 2, 3, 4, 5 };
        p.state.append (raw, sizeof (raw));
        s.plugins.add (p);

        auto tree = toValueTree (s);
        auto r = fromValueTree (tree);

        expectEquals (r.inputDevice, s.inputDevice);
        expectEquals (r.outputDevice, s.outputDevice);
        expectEquals (r.sampleRate, 48000.0);
        expectEquals (r.bufferSize, 128);
        expectEquals (r.pluginFolders.size(), 1);
        expectEquals (r.pluginFolders[0], juce::String ("C:/My VST3"));
        expectEquals (r.windowState, juce::String ("0 0 600 480"));
        expect (r.updateCheckEnabled == true);
        expect (r.updateCheckAsked == true);
        expectEquals (r.lastNotifiedVersion, juce::String ("1.0.3"));
        expectEquals (r.plugins.size(), 1);
        expectEquals (r.plugins[0].fileOrId, p.fileOrId);
        expect (r.plugins[0].bypassed == true);
        expect (r.plugins[0].state == p.state);
    }
};
static PersistenceTest persistenceTest;

// JUCEs Logger landet auf Windows in OutputDebugString und ist damit weder im Terminal
// noch im CI-Log zu sehen -- ein fehlgeschlagener Test meldete sich nur über den Exit-Code.
// Darum die Meldungen zusätzlich nach stdout.
struct PrintingTestRunner : juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};

int main (int, char**)
{
    PrintingTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    if (failures > 0)
    {
        juce::Logger::writeToLog ("TESTS FAILED: " + juce::String (failures));
        return 1;
    }
    juce::Logger::writeToLog ("All tests completed successfully");
    return 0;
}
