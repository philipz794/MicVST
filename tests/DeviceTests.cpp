#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "audio/MicVSTDeviceManager.h"

namespace
{
    class MockAudioIODevice final : public juce::AudioIODevice
    {
    public:
        MockAudioIODevice (juce::String devName,
                           juce::String typeNameIn,
                           juce::String inNameIn,
                           juce::String outNameIn,
                           bool failMismatchIn)
            : AudioIODevice (std::move (devName), std::move (typeNameIn)),
              inName (std::move (inNameIn)),
              outName (std::move (outNameIn)),
              failSampleRateMismatch (failMismatchIn)
        {
        }

        juce::StringArray getOutputChannelNames() override
        {
            return outName.isNotEmpty() ? juce::StringArray { "Out 1", "Out 2" } : juce::StringArray {};
        }

        juce::StringArray getInputChannelNames() override
        {
            return inName.isNotEmpty() ? juce::StringArray { "In 1", "In 2" } : juce::StringArray {};
        }

        juce::Array<double> getAvailableSampleRates() override
        {
            if (failSampleRateMismatch)
                return {};
            return { 48000.0 };
        }

        juce::Array<int> getAvailableBufferSizes() override
        {
            if (getTypeName() == MicVSTDeviceManager::lowLatencyTypeName)
                return { 480, 960 };
            return { 480 };
        }

        int getDefaultBufferSize() override
        {
            return inName.contains ("96k") && outName.isEmpty() ? 960 : 480;
        }

        juce::String open (const juce::BigInteger& inputs,
                           const juce::BigInteger& outputs,
                           double sr,
                           int bs) override
        {
            if (failSampleRateMismatch)
            {
                lastError = "The input and output devices don't share a common sample rate!";
                return lastError;
            }
            lastError.clear();
            inChannels = inputs;
            outChannels = outputs;
            sampleRate = sr > 0.0 ? sr : 48000.0;
            blockSize = bs > 0 ? bs : getDefaultBufferSize();
            opened = true;
            return {};
        }

        void close() override
        {
            stop();
            opened = false;
        }

        bool isOpen() override { return opened; }

        void start (juce::AudioIODeviceCallback* cb) override
        {
            if (! opened || cb == nullptr)
                return;
            callback = cb;
            callback->audioDeviceAboutToStart (this);
            playing = true;
        }

        void stop() override
        {
            if (playing)
            {
                playing = false;
                if (callback != nullptr)
                    callback->audioDeviceStopped();
            }
        }

        bool isPlaying() override { return playing; }
        juce::String getLastError() override { return lastError; }
        int getCurrentBufferSizeSamples() override { return blockSize; }
        double getCurrentSampleRate() override { return sampleRate; }
        int getCurrentBitDepth() override { return 32; }
        juce::BigInteger getActiveOutputChannels() const override { return outChannels; }
        juce::BigInteger getActiveInputChannels() const override { return inChannels; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }

        // Simuliert einen verspäteten JUCE-WASAPI-Callback nach audioDeviceStopped().
        void fireSpuriousCallbackAfterStop (int numIns, int numOuts, int numSamples)
        {
            if (callback == nullptr)
                return;

            juce::AudioBuffer<float> inBuf (juce::jmax (1, numIns), numSamples);
            juce::AudioBuffer<float> outBuf (juce::jmax (1, numOuts), numSamples);
            inBuf.clear();
            for (int ch = 0; ch < numOuts; ++ch)
                outBuf.setSample (ch, 0, 123.0f);

            const float* const* ins = numIns > 0 ? inBuf.getArrayOfReadPointers() : nullptr;
            float* const* outs      = numOuts > 0 ? outBuf.getArrayOfWritePointers() : nullptr;
            callback->audioDeviceIOCallbackWithContext (ins, numIns, outs, numOuts, numSamples, {});
        }

    private:
        juce::String inName, outName, lastError;
        bool failSampleRateMismatch = false;
        bool opened = false;
        bool playing = false;
        double sampleRate = 48000.0;
        int blockSize = 480;
        juce::BigInteger inChannels, outChannels;
        juce::AudioIODeviceCallback* callback = nullptr;
    };

    class MockWASAPIDeviceType final : public juce::AudioIODeviceType
    {
    public:
        MockWASAPIDeviceType (juce::String typeNameIn, bool supportsSRCIn)
            : AudioIODeviceType (std::move (typeNameIn)),
              supportsSRC (supportsSRCIn)
        {
        }

        void scanForDevices() override {}

        juce::StringArray getDeviceNames (bool wantInputNames) const override
        {
            return wantInputNames ? inputNames : outputNames;
        }

        int getDefaultDeviceIndex (bool) const override { return 0; }

        int getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const override
        {
            if (device == nullptr)
                return -1;
            return getDeviceNames (asInput).indexOf (device->getName());
        }

        bool hasSeparateInputsAndOutputs() const override { return true; }

        juce::AudioIODevice* createDevice (const juce::String& outputDeviceName,
                                           const juce::String& inputDeviceName) override
        {
            const bool hasOut = outputDeviceName.isEmpty() || outputNames.contains (outputDeviceName);
            const bool hasIn  = inputDeviceName.isEmpty()  || inputNames.contains (inputDeviceName);
            if (! hasOut || ! hasIn || (outputDeviceName.isEmpty() && inputDeviceName.isEmpty()))
                return nullptr;

            const bool mismatch = ! supportsSRC
                               && inputDeviceName.contains ("96k")
                               && outputDeviceName.contains ("48k");
            const auto devName = outputDeviceName.isNotEmpty() ? outputDeviceName : inputDeviceName;
            return new MockAudioIODevice (devName, getTypeName(), inputDeviceName, outputDeviceName, mismatch);
        }

        juce::StringArray inputNames  { "USB Mic 96k", "Mic 48k" };
        juce::StringArray outputNames { "CABLE Input (VB-Audio Virtual Cable) 48k" };
        bool supportsSRC = false;
    };

    void installMockWasapiTypes (MicVSTDeviceManager& dm)
    {
        dm.closeAudioDevice();
        while (! dm.getAvailableDeviceTypes().isEmpty())
            dm.removeAudioDeviceType (dm.getAvailableDeviceTypes().getFirst());

        dm.addAudioDeviceType (std::make_unique<MockWASAPIDeviceType> (
            MicVSTDeviceManager::lowLatencyTypeName, false));
        dm.addAudioDeviceType (std::make_unique<MockWASAPIDeviceType> (
            MicVSTDeviceManager::sharedTypeName, true));
    }
}

struct DeviceTests : juce::UnitTest
{
    DeviceTests() : juce::UnitTest ("DeviceSelectionAndCallback") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI juceInit;

        beginTest ("sample-rate mismatch in low-latency falls back to shared mode and preserves names");
        {
            AudioEngine engine;
            auto& dm = engine.getDeviceManager();
            installMockWasapiTypes (dm);

            engine.initialise ("USB Mic 96k", "");
            expect (engine.isRunning());
            expectEquals (dm.getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::lowLatencyTypeName));

            // Mismatched pair: Low-Latency scheitert wegen fehlender gemeinsamer Samplerate -> Fallback auf Shared.
            engine.setDeviceConfig ("USB Mic 96k", "CABLE Input (VB-Audio Virtual Cable) 48k", 0.0, 0);
            expect (engine.isRunning());
            expectEquals (dm.getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::sharedTypeName));

            const auto setup = dm.getAudioDeviceSetup();
            expectEquals (setup.inputDeviceName,  juce::String ("USB Mic 96k"));
            expectEquals (setup.outputDeviceName, juce::String ("CABLE Input (VB-Audio Virtual Cable) 48k"));
            expectEquals (dm.getCurrentDeviceTypeObject()->getDeviceNames (true).size(), 2);

            // Zurück zu einem kompatiblen Paar -> wieder Low-Latency-Modus.
            engine.setDeviceConfig ("Mic 48k", "CABLE Input (VB-Audio Virtual Cable) 48k", 0.0, 0);
            expect (engine.isRunning());
            expectEquals (dm.getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::lowLatencyTypeName));

            // Wechsel auf Input-only und wieder zurück auf mismatched Pair.
            engine.setDeviceConfig ("USB Mic 96k", "", 0.0, 0);
            expect (engine.isRunning());
            engine.setDeviceConfig ("USB Mic 96k", "CABLE Input (VB-Audio Virtual Cable) 48k", 0.0, 0);
            expect (engine.isRunning());
            expectEquals (dm.getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::sharedTypeName));
        }

        beginTest ("spurious audio callback after audioDeviceStopped does not crash");
        {
            AudioEngine engine;
            auto& dm = engine.getDeviceManager();
            installMockWasapiTypes (dm);

            engine.initialise ("USB Mic 96k", "");
            expect (engine.isRunning());

            auto* mockDev = dynamic_cast<MockAudioIODevice*> (dm.getCurrentAudioDevice());
            expect (mockDev != nullptr);

            // Stoppt das Device (setzt AudioProcessorPlayer::tempBuffer auf (1,1) / sampleRate=0)
            // und feuert danach einen späten WASAPI-Callback wie beim echten Crash (2 in, 0 out, 960 samples).
            mockDev->stop();
            expect (! engine.isRunning());
            mockDev->fireSpuriousCallbackAfterStop (2, 0, 960);

            // Erneutes Öffnen (2 in, 2 out) -> stop() -> späten 2-in/2-out-Callback testen.
            engine.setDeviceConfig ("USB Mic 96k", "CABLE Input (VB-Audio Virtual Cable) 48k", 0.0, 0);
            expect (engine.isRunning());
            mockDev = dynamic_cast<MockAudioIODevice*> (dm.getCurrentAudioDevice());
            expect (mockDev != nullptr);
            mockDev->stop();
            expect (! engine.isRunning());
            mockDev->fireSpuriousCallbackAfterStop (2, 2, 480);

            // Erneutes Öffnen muss danach sauber funktionieren.
            engine.setDeviceConfig ("Mic 48k", "CABLE Input (VB-Audio Virtual Cable) 48k", 0.0, 0);
            expect (engine.isRunning());
        }

        beginTest ("detectCableOutput does not switch away from active shared mode");
        {
            AudioEngine engine;
            auto& dm = engine.getDeviceManager();
            installMockWasapiTypes (dm);

            engine.initialise ("USB Mic 96k", "CABLE Input (VB-Audio Virtual Cable) 48k");
            expect (engine.isRunning());
            expectEquals (dm.getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::sharedTypeName));
            auto* devBefore = dm.getCurrentAudioDevice();

            const auto cable = engine.detectCableOutput();
            expectEquals (cable, juce::String ("CABLE Input (VB-Audio Virtual Cable) 48k"));
            expectEquals (dm.getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::sharedTypeName));
            expect (dm.getCurrentAudioDevice() == devBefore);
            expect (engine.isRunning());
        }
    }
};

static DeviceTests deviceTests;
