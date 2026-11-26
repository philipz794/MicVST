#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

// AudioDeviceManager mit WASAPI "Low Latency Mode" (IAudioClient3, ab Win 10) als
// bevorzugtem Typ und klassischem Shared als Fallback. Low Latency meldet pro Gerät
// echte Buffer-Größen (min..max in Treiber-Schritten) -> Buffer-Dropdown im DevicePanel;
// Shared meldet genau EINE Größe (Windows-Mixer-Periode), das Dropdown bleibt dann weg.
// Exclusive bleibt bewusst außen vor (lockt Geräte gegen andere Apps).
struct MicVSTDeviceManager : juce::AudioDeviceManager
{
    static constexpr const char* lowLatencyTypeName = "Windows Audio (Low Latency Mode)";
    static constexpr const char* sharedTypeName     = "Windows Audio";

    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        if (auto* ll = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
                           juce::WASAPIDeviceMode::sharedLowLatency))
            types.add (ll);
        if (auto* shared = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
                               juce::WASAPIDeviceMode::shared))
            types.add (shared);
    }

    juce::String preferredTypeName()
    {
        for (auto* t : getAvailableDeviceTypes())
            if (t->getTypeName() == lowLatencyTypeName)
                return lowLatencyTypeName;
        return sharedTypeName;
    }

    juce::StringArray orderedTypeNames()
    {
        juce::StringArray available;
        for (auto* t : getAvailableDeviceTypes())
            available.add (t->getTypeName());

        juce::StringArray ordered;
        for (auto* preferred : { lowLatencyTypeName, sharedTypeName })
            if (available.contains (preferred))
                ordered.add (preferred);

        for (auto& name : available)
            if (! ordered.contains (name))
                ordered.add (name);

        return ordered;
    }

    static juce::String openSetupWithType (juce::AudioDeviceManager& dm,
                                           const juce::String& typeName,
                                           const juce::String& input,
                                           const juce::String& output,
                                           double sampleRate,
                                           int bufferSize)
    {
        if (input.isEmpty() && output.isEmpty())
        {
            auto setup = dm.getAudioDeviceSetup();
            setup.inputDeviceName.clear();
            setup.outputDeviceName.clear();
            setup.useDefaultInputChannels  = false;
            setup.useDefaultOutputChannels = false;
            setup.inputChannels.clear();
            setup.outputChannels.clear();
            return dm.setAudioDeviceSetup (setup, true);
        }

        juce::AudioIODeviceType* targetType = nullptr;
        for (auto* t : dm.getAvailableDeviceTypes())
        {
            if (t->getTypeName() == typeName)
            {
                targetType = t;
                break;
            }
        }
        if (targetType == nullptr)
            return "Audio device type not available: " + typeName;

        if ((input.isNotEmpty()  && ! targetType->getDeviceNames (true).contains (input))
         || (output.isNotEmpty() && ! targetType->getDeviceNames (false).contains (output)))
        {
            targetType->scanForDevices();
        }

        if (dm.getCurrentAudioDeviceType() != typeName)
        {
            dm.closeAudioDevice();

            juce::XmlElement xml ("DEVICESETUP");
            xml.setAttribute ("deviceType", typeName);
            xml.setAttribute ("audioInputDeviceName", input);
            xml.setAttribute ("audioOutputDeviceName", output);
            xml.setAttribute ("audioDeviceInChans", "11");
            xml.setAttribute ("audioDeviceOutChans", "11");
            if (sampleRate > 0.0)
                xml.setAttribute ("audioDeviceRate", sampleRate);
            if (bufferSize > 0)
                xml.setAttribute ("audioDeviceBufferSize", bufferSize);

            return dm.initialise (2, 2, &xml, false);
        }

        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName  = input;
        setup.outputDeviceName = output;
        setup.sampleRate = sampleRate > 0.0 ? sampleRate : 0.0;
        setup.bufferSize = bufferSize > 0 ? bufferSize : 0;
        setup.useDefaultInputChannels  = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();  setup.inputChannels.setRange (0, 2, true);
        setup.outputChannels.clear(); setup.outputChannels.setRange (0, 2, true);
        return dm.setAudioDeviceSetup (setup, true);
    }

    static juce::String openSetupWithFallback (juce::AudioDeviceManager& dm,
                                               const juce::StringArray& typeOrder,
                                               const juce::String& input,
                                               const juce::String& output,
                                               double sampleRate,
                                               int bufferSize)
    {
        if (input.isEmpty() && output.isEmpty())
            return openSetupWithType (dm, dm.getCurrentAudioDeviceType(), input, output, sampleRate, bufferSize);

        juce::String lastErr;
        for (int i = 0; i < typeOrder.size(); ++i)
        {
            const double sr = (i == 0) ? sampleRate : (sampleRate > 0.0 ? sampleRate : 48000.0);
            const int   buf = (i == 0) ? bufferSize : 0;

            lastErr = openSetupWithType (dm, typeOrder[i], input, output, sr, buf);
            if (lastErr.isEmpty())
                return {};

            if (i + 1 < typeOrder.size())
                juce::Logger::writeToLog ("Audio: " + typeOrder[i] + " failed (" + lastErr
                                          + ") -> falling back to " + typeOrder[i + 1]);
        }
        return lastErr;
    }
};
