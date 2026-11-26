#include "ui/DevicePanel.h"

DevicePanel::DevicePanel (AudioEngine& e) : engine (e)
{
    for (auto* l : { &inLabel, &outLabel })
        l->setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (inLabel);  addAndMakeVisible (inBox);
    addAndMakeVisible (outLabel); addAndMakeVisible (outBox);

    inInfo.setTooltip ("Select your microphone here");
    outInfo.setTooltip ("Select \"CABLE Input\" here, "
                        "and choose \"CABLE Output\" as the microphone in Discord etc.");
    addAndMakeVisible (inInfo);
    addAndMakeVisible (outInfo);

    bufInfo.setTooltip ("Buffer size in samples. Smaller = lower latency, higher CPU risk. "
                        "\"Auto\" uses the device default. Only shown when the devices "
                        "support multiple sizes (low-latency mode).");
    addChildComponent (bufLabel); addChildComponent (bufBox); addChildComponent (bufInfo);
    bufLabel.setJustificationType (juce::Justification::centredLeft);
    bufBox.onChange = [this] { apply(); };

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (statusLabel);

    inBox.onChange  = [this] { apply(); };
    outBox.onChange = [this] { apply(); };

    engine.getDeviceManager().addChangeListener (this);
    refresh();
    startTimer (500);   // Info-Zeile (v. a. Geräte-/Plugin-Latenz) live halten
}

DevicePanel::~DevicePanel()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
}

void DevicePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();   // Gerät extern geändert (z. B. ab-/angesteckt) -> Combos spiegeln
}

void DevicePanel::timerCallback()
{
    updateStatus();   // nur die Info-Zeile; Combos nicht anfassen (User könnte gerade wählen)
}

void DevicePanel::refresh()
{
    updating = true;   // verhindert, dass das Befüllen apply() auslöst

    inBox.clear (juce::dontSendNotification);
    outBox.clear (juce::dontSendNotification);

    auto& dm = engine.getDeviceManager();
    const auto setup = dm.getAudioDeviceSetup();

    if (auto* type = dm.getCurrentDeviceTypeObject())
    {
        if (type->getDeviceNames (true).isEmpty() && type->getDeviceNames (false).isEmpty())
            type->scanForDevices();

        const auto ins = type->getDeviceNames (true);
        for (int i = 0; i < ins.size(); ++i) inBox.addItem (ins[i], i + 1);
        const int si = ins.indexOf (setup.inputDeviceName);
        if (si >= 0) inBox.setSelectedId (si + 1, juce::dontSendNotification);

        outBox.addItem ("(none)", 1);   // leerer Output-Name = kein Host-Output
        const auto outs = type->getDeviceNames (false);
        for (int i = 0; i < outs.size(); ++i) outBox.addItem (outs[i], i + 2);
        const int so = outs.indexOf (setup.outputDeviceName);
        outBox.setSelectedId (setup.outputDeviceName.isNotEmpty() && so >= 0 ? so + 2 : 1,
                              juce::dontSendNotification);
    }

    // Buffer-Zeile: nur wenn das aktuelle Gerät mehrere Größen anbietet (Low-Latency-Modus).
    bufBox.clear (juce::dontSendNotification);
    auto* dev = dm.getCurrentAudioDevice();
    const auto sizes = dev != nullptr ? dev->getAvailableBufferSizes() : juce::Array<int>();
    const bool showBuf = sizes.size() > 1;
    if (showBuf)
    {
        const double sr = dev->getCurrentSampleRate();
        bufBox.addItem ("Auto (default)", 1);
        for (int i = 0; i < sizes.size(); ++i)
            bufBox.addItem (juce::String (sizes[i]) + " samples ("
                                + juce::String (sizes[i] / sr * 1000.0, 1) + " ms)", i + 2);
        const int pref = engine.getPreferredBufferSize();
        const int idx = sizes.indexOf (pref);
        bufBox.setSelectedId (pref > 0 && idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);

        // Der gewählte Wert existiert beim aktuellen Gerät nicht mehr (z. B. Gerät
        // gewechselt) -> Anzeige fällt auf "Auto" zurück, also muss auch der Engine-
        // Zustand nachziehen, sonst würde captureState() einen nie bestätigten Wert
        // persistieren. Nur bei sichtbarer Zeile: bei !showBuf bleibt die Wahl erhalten
        // (z. B. USB-Interface kurz getrennt) -- JUCE klemmt unpassende Größen ohnehin.
        if (pref > 0 && idx < 0)
            engine.setPreferredBufferSize (0);
    }
    if (showBuf != bufBox.isVisible())
    {
        bufLabel.setVisible (showBuf); bufBox.setVisible (showBuf); bufInfo.setVisible (showBuf);
        if (auto* p = getParentComponent()) p->resized();   // MainComponent-Layout nachziehen
    }

    updateStatus();

    updating = false;
}

void DevicePanel::updateStatus()
{
    auto& dm = engine.getDeviceManager();

    juce::String st = engine.isRunning() ? juce::String ("Active")
                                         : juce::String ("Idle - device disconnected");
    if (auto* dev = dm.getCurrentAudioDevice())
    {
        const double sr  = dev->getCurrentSampleRate();
        const int    buf = dev->getCurrentBufferSizeSamples();
        st << "   |   " << juce::String (sr, 0) << " Hz";
        st << "   |   buffer " << juce::String (buf);

        // Ende-zu-Ende durch MicVST: Geräte-Latenz (In+Out) + ein Block + Plugin-Latenz
        // des Graphen (Lookahead-Plugins melden diese via getLatencySamples()).
        const int    pluginLatency = engine.getGraph().getLatencySamples();
        const double lat = (dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples()
                            + buf + pluginLatency) / sr * 1000.0;
        st << "   |   latency " << juce::String (lat, 1) << " ms";
    }
    statusLabel.setText (st, juce::dontSendNotification);
}

void DevicePanel::apply()
{
    if (updating) return;

    auto& dm = engine.getDeviceManager();
    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr) return;

    const auto ins  = type->getDeviceNames (true);
    const auto outs = type->getDeviceNames (false);

    const int ii = inBox.getSelectedId() - 1;
    juce::String input = juce::isPositiveAndBelow (ii, ins.size()) ? ins[ii] : juce::String();

    juce::String output;   // "(none)" (id 1) -> leer
    const int oid = outBox.getSelectedId();
    if (oid >= 2 && juce::isPositiveAndBelow (oid - 2, outs.size())) output = outs[oid - 2];

    // Buffer-Wunsch: id 1 = Auto (0). Bei Auto den Geräte-Default explizit setzen,
    // weil setDeviceConfig 0 als "unverändert" interpretiert (sonst kein Zurückstellen).
    int buf = 0;
    if (bufBox.isVisible() && bufBox.getSelectedId() >= 2)
    {
        auto* dev = dm.getCurrentAudioDevice();
        const auto sizes = dev != nullptr ? dev->getAvailableBufferSizes() : juce::Array<int>();
        const int idx = bufBox.getSelectedId() - 2;
        if (juce::isPositiveAndBelow (idx, sizes.size())) buf = sizes[idx];
    }
    engine.setPreferredBufferSize (buf);
    engine.setDeviceConfig (input, output, 0.0, buf);
}

void DevicePanel::resized()
{
    constexpr int rowH = 26, labelW = 84, gap = 4;
    auto r = getLocalBounds();

    auto row = [&]
    {
        auto a = r.removeFromTop (rowH);
        r.removeFromTop (gap);
        return a;
    };

    // Label + "?"-Icon in der Label-Spalte, danach die ComboBox.
    auto labelWithInfo = [labelW] (juce::Rectangle<int> r, juce::Label& lbl, InfoIcon& info)
    {
        auto col = r.removeFromLeft (labelW);
        info.setBounds (col.removeFromRight (18).reduced (2));
        lbl.setBounds (col);
        return r;
    };

    auto inRow = row();
    inBox.setBounds (labelWithInfo (inRow, inLabel, inInfo));
    auto outRow = row();
    outBox.setBounds (labelWithInfo (outRow, outLabel, outInfo));

    if (bufBox.isVisible())
    {
        auto bufRow = row();
        bufBox.setBounds (labelWithInfo (bufRow, bufLabel, bufInfo));
    }

    statusLabel.setBounds (row());   // dauerhafte Info-Zeile unter den Geräten
}

int DevicePanel::preferredHeight() const
{
    // 3 Zeilen à 26 + 2 Lücken à 4 = 86; Buffer-Zeile sichtbar -> 4 Zeilen + 3 Lücken = 116.
    return bufBox.isVisible() ? 116 : 86;
}
