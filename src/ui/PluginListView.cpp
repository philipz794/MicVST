#include "ui/PluginListView.h"
#include "audio/PluginChain.h"
#include "ui/PluginPicker.h"

// ============================ TrashButton ============================

void PluginListView::TrashButton::paintButton (juce::Graphics& g, bool highlighted, bool /*down*/)
{
    auto b = getLocalBounds().toFloat();
    const auto col = highlighted ? juce::Colours::orangered : juce::Colours::lightgrey;
    g.setColour (col);

    const float cx = b.getCentreX();
    const float iconH = 15.0f;
    const float top = b.getCentreY() - iconH * 0.5f;
    const float lidY = top + 3.0f;
    const float botY = top + iconH;

    // Deckel-Linie + Griff oben.
    g.drawLine (cx - 7.0f, lidY, cx + 7.0f, lidY, 1.3f);
    g.drawLine (cx - 2.5f, lidY, cx - 2.5f, top + 1.0f, 1.3f);
    g.drawLine (cx - 2.5f, top + 1.0f, cx + 2.5f, top + 1.0f, 1.3f);
    g.drawLine (cx + 2.5f, top + 1.0f, cx + 2.5f, lidY, 1.3f);

    // Korpus (leicht trapezförmig).
    g.drawLine (cx - 5.5f, lidY, cx - 4.5f, botY, 1.3f);
    g.drawLine (cx + 5.5f, lidY, cx + 4.5f, botY, 1.3f);
    g.drawLine (cx - 4.5f, botY, cx + 4.5f, botY, 1.3f);

    // Innere Striche.
    g.drawLine (cx - 2.0f, lidY + 2.5f, cx - 2.0f, botY - 1.5f, 1.0f);
    g.drawLine (cx + 2.0f, lidY + 2.5f, cx + 2.0f, botY - 1.5f, 1.0f);
}

// ============================ Row ============================

PluginListView::HotkeyButton::HotkeyButton (PluginListView& ownerIn, int idx)
    : juce::Button ("Hotkey"), owner (ownerIn), index (idx)
{
    setWantsKeyboardFocus (true);
    conflicted = owner.conflictingHotkeys.contains (index);

    // Der Tooltip nennt den gesetzten Hotkey ausgeschrieben: lange Kombinationen wie
    // "Ctrl+Shift+Num+" passen nicht ins Feld und werden dort abgekuerzt.
    const auto& es = owner.engine.getChain().entries();
    const auto hk = juce::isPositiveAndBelow (index, (int) es.size()) ? es[(size_t) index].hotkey
                                                                      : Hotkey {};
    juce::String tip;
    if (hk.isSet())
    {
        tip << "Hotkey: " << hk.toDisplayString() << " - toggles bypass for this plugin "
            << "from anywhere.\n";
        if (hk.isModifierless())
            tip << "No modifier key, so it also fires while you use this key in other "
                   "applications.\n";
        if (conflicted)
            tip << "Not working: MicVST could not watch the keyboard.\n";
        tip << "\nClick and press another key to change it, or Esc to clear it.";
    }
    else
    {
        tip << "Global hotkey to bypass this plugin - click, then press a key or combination. "
            << "Letters, digits, F1-F12 and the numeric keypad are accepted, with or without "
            << "Ctrl/Alt/Shift/Win. Esc clears.";
    }
    setTooltip (tip);
    onClick = [this]
    {
        // Waehrend der Aufnahme die eigenen Hotkeys abmelden: sonst wuerde das Druecken
        // einer schon belegten Kombination NEBENBEI das zugehoerige Plugin umschalten.
        capturing = true;
        owner.hotkeys.setBindings ({});
        grabKeyboardFocus();
        repaint();
    };
}

void PluginListView::HotkeyButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto& es = owner.engine.getChain().entries();
    const auto hk = juce::isPositiveAndBelow (index, (int) es.size()) ? es[(size_t) index].hotkey
                                                                      : Hotkey {};
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (capturing ? juce::Colours::darkorange
                           : (highlighted ? juce::Colour (0xff3a3a3a) : juce::Colour (0xff303030)));
    g.fillRoundedRectangle (b, 3.0f);

    if (! capturing)
    {
        // Rot = funktioniert nicht (der Tastatur-Hook liess sich nicht installieren).
        // Gelb = ohne Modifier, loest daher auch beim normalen Tippen in anderen
        // Programmen aus.
        if (conflicted)
        {
            g.setColour (juce::Colours::orangered);
            g.drawRoundedRectangle (b, 3.0f, 1.0f);
        }
        else if (hk.isModifierless())
        {
            g.setColour (juce::Colours::gold);
            g.drawRoundedRectangle (b, 3.0f, 1.0f);
        }
    }

    g.setColour (capturing ? juce::Colours::black
                           : (hk.isSet() ? juce::Colours::white : juce::Colours::grey));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    // Ellipse an: eine lange Kombination ("Ctrl+Shift+Num+") passt nicht ins Feld, und ein
    // sichtbares "..." sagt das -- ausgeschrieben steht sie im Tooltip.
    g.drawText (capturing ? "press..." : (hk.isSet() ? hk.toDisplayString() : "hotkey"),
                getLocalBounds().reduced (3, 0), juce::Justification::centred, true);
}

bool PluginListView::HotkeyButton::keyPressed (const juce::KeyPress& kp)
{
    if (! capturing) return false;

    if (kp.getKeyCode() == juce::KeyPress::escapeKey)
    {
        capturing = false;
        owner.setHotkey (index, {});   // loeschen (registriert anschliessend neu)
        return true;
    }

    // Den physisch gedrueckten Virtual-Key und die echten Modifier direkt abfragen, statt
    // JUCEs Tastencode zu uebersetzen: JUCE liefert fuer die meisten druckbaren Tasten das
    // ZEICHEN (Numpad-"+" kommt als '+' an, nicht als VK_ADD), und juce::ModifierKeys kennt
    // die Windows-Taste nicht. Nur wenn das fehlschlaegt, auf JUCEs Code zurueckfallen.
    const int vk = pressedHotkeyVk();
    const auto m = kp.getModifiers();
    const auto hk = vk != 0
        ? hotkeyFromVirtualKey (vk, heldHotkeyModifiers())
        : hotkeyFromKeyCode (kp.getKeyCode(), m.isCtrlDown(), m.isAltDown(), m.isShiftDown(), false);

    if (! hk.isSet())
        return true;   // unbrauchbare Taste: im Aufnahmemodus bleiben, nichts aendern

    capturing = false;

    // Ohne Modifier beobachtet MicVST die Taste nur (sie wird nicht verschluckt), loest
    // also auch aus, wenn man sie woanders normal benutzt. Das ist bei einem Buchstaben
    // oder einer Ziffer fast immer ein Versehen -> nachfragen, statt es stillschweigend
    // zu uebernehmen.
    if (hk.isModifierless())
    {
        owner.confirmModifierlessHotkey (index, hk);
        repaint();
        return true;
    }

    owner.setHotkey (index, hk);
    return true;
}

void PluginListView::HotkeyButton::focusLost (FocusChangeType)
{
    if (! capturing) return;

    // Abgebrochen (woanders hingeklickt): nichts aendern, aber die abgemeldeten Hotkeys
    // wieder registrieren.
    capturing = false;
    owner.applyHotkeyBindings();
    repaint();
}

PluginListView::Row::Row (PluginListView& ownerIn, int idx)
    : owner (ownerIn), index (idx), hotkeyBtn (ownerIn, idx)
{
    const auto& es = owner.engine.getChain().entries();
    const bool bypassed = juce::isPositiveAndBelow (index, (int) es.size())
                          && es[(size_t) index].bypassed;

    bypassBtn.setButtonText ("Bypass");
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setToggleState (bypassed, juce::dontSendNotification);
    bypassBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkorange);
    bypassBtn.onClick = [this] { owner.toggleBypass (index); };
    addAndMakeVisible (bypassBtn);

    addAndMakeVisible (hotkeyBtn);

    trashBtn.onClick = [this] { owner.requestRemove (index); };
    addAndMakeVisible (trashBtn);
}

bool PluginListView::Row::overHandle (juce::Point<int> p) const { return p.x < 24; }

void PluginListView::Row::resized()
{
    auto r = getLocalBounds().reduced (2);
    trashBtn.setBounds (r.removeFromRight (28));
    r.removeFromRight (4);
    bypassBtn.setBounds (r.removeFromRight (80));
    r.removeFromRight (4);
    // 92 px fassen die gaengigen Kombinationen bis "Ctrl+Shift+F12"; laengeres wird
    // abgekuerzt und steht im Tooltip.
    hotkeyBtn.setBounds (r.removeFromRight (92));
}

void PluginListView::Row::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    const auto& es = owner.engine.getChain().entries();
    if (! juce::isPositiveAndBelow (index, (int) es.size())) return;
    const auto& e = es[(size_t) index];

    g.setColour (dragging ? juce::Colours::darkblue
                          : (index % 2 ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff242424)));
    g.fillRect (b);

    // Drag-Handle: zwei Spalten à drei Punkte.
    g.setColour (juce::Colours::grey);
    for (int cxn = 0; cxn < 2; ++cxn)
        for (int cyn = 0; cyn < 3; ++cyn)
            g.fillEllipse ((float) (8 + cxn * 6), (float) (b.getCentreY() - 6 + cyn * 6), 2.6f, 2.6f);

    // Latenz-Spalte links vom Hotkey-Feld: gemeldete Plugin-Latenz in ms (Lookahead etc.).
    constexpr int latW = 72;
    const int latRight = hotkeyBtn.getX() - 8;
    const int latLeft  = latRight - latW;

    juce::String latText;
    if (auto* node = owner.engine.getGraph().getNodeForId (e.node))
        if (auto* proc = node->getProcessor())
        {
            auto* dev = owner.engine.getDeviceManager().getCurrentAudioDevice();
            const double sr = dev != nullptr ? dev->getCurrentSampleRate() : 48000.0;
            const double ms = sr > 0.0 ? (double) proc->getLatencySamples() / sr * 1000.0 : 0.0;
            latText = juce::String (ms, 1) + " ms";
        }

    // Name (auf den Platz links der Latenz-Spalte begrenzt).
    const auto name = e.fileOrId == PluginChain::monoToStereoId ? juce::String ("Mono -> Stereo")
                    : e.fileOrId == PluginChain::stereoToMonoId ? juce::String ("Stereo -> Mono")
                    : juce::File (e.fileOrId).getFileNameWithoutExtension();
    g.setColour (e.bypassed ? juce::Colours::grey : juce::Colours::white);
    g.drawText (name, 28, 0, latLeft - 6 - 28, b.getHeight(), juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (latText, latLeft, 0, latW, b.getHeight(), juce::Justification::centredRight);
}

void PluginListView::Row::mouseDown (const juce::MouseEvent& e)
{
    if (overHandle (e.getPosition()))
    {
        dragging = true;
        grabOffsetY = e.getPosition().y;
        toFront (false);
    }
}

void PluginListView::Row::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const int n = owner.rows.size();
    const int maxY = juce::jmax (0, (n - 1) * rowH);
    const int newY = juce::jlimit (0, maxY, e.getEventRelativeTo (getParentComponent()).y - grabOffsetY);
    setTopLeftPosition (getX(), newY);
}

void PluginListView::Row::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    const int n = owner.rows.size();
    const int target = juce::jlimit (0, n - 1, (getY() + rowH / 2) / rowH);
    owner.requestMove (index, target);   // rebuildRows() snappt alles zurück an Position
}

void PluginListView::Row::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! overHandle (e.getPosition()))
        owner.openEditor (index);
}

// ============================ PluginListView ============================

PluginListView::PluginListView (AudioEngine& e) : engine (e)
{
    addAndMakeVisible (addBtn);
    addBtn.onClick = [this] { showPluginPicker(); };

    addAndMakeVisible (manageFoldersBtn);
    manageFoldersBtn.onClick = [this] { showFolderMenu(); };

    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    scanLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    scanLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addChildComponent (scanLabel);       // nur sichtbar während des Scans
    addChildComponent (scanBar);
    addChildComponent (skipScanBtn);
    skipScanBtn.setTooltip ("Skip the plugin that is currently being scanned");
    skipScanBtn.onClick = [this] { engine.skipCurrentScanFile(); };
    skipLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    skipLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    addChildComponent (skipLabel);

    engine.onScanProgress = [this] (int cur, int total, juce::String name)
    {
        scanProgress = total > 0 ? (double) (cur - 1) / (double) total : 0.0;
        scanLabel.setText ("Scanning plugins... " + juce::String (cur) + "/" + juce::String (total)
                           + " - " + name, juce::dontSendNotification);
        updateScanUi();
    };
    engine.onScanFinished = [this]
    {
        applyHotkeyBindings();   // Kette kann per deferred Restore gerade erst entstanden sein
        rebuildRows();
        updateScanUi();
    };
    // Dasselbe, aber mitten im laufenden Scan: die Kette steht schon, der Scan-Status
    // (Fortschrittsbalken, Skip-Button) bleibt unverändert -> kein updateScanUi.
    engine.onChainRestored = [this] { applyHotkeyBindings(); rebuildRows(); };

    // Hotkey drücken = dieses Plugin bypassen/aktivieren. Die Hotkey-ID ist der
    // Ketten-Index; toggleBypass prüft die Grenzen selbst (die Kette kann sich zwischen
    // Registrierung und Tastendruck geändert haben).
    hotkeys.onHotkey = [this] (int index) { toggleBypass (index); };
    updateScanUi();

    applyHotkeyBindings();   // gespeicherte Hotkeys der wiederhergestellten Kette anmelden
    rebuildRows();
    startTimer (500);   // Plugin-Latenz live halten (z. B. wenn ein Plugin Lookahead an-/abschaltet)
}

// Engine-Callbacks lösen: eine zerstörte View darf nie mehr zurückgerufen werden.
PluginListView::~PluginListView()
{
    engine.onScanProgress  = nullptr;
    engine.onScanFinished  = nullptr;
    engine.onChainRestored = nullptr;
}

void PluginListView::timerCallback()
{
    for (auto* r : rows)
        r->repaint();   // nur neu zeichnen; die Latenz wird in Row::paint frisch gelesen
}

void PluginListView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (30);
    constexpr int btnH = 26;   // einheitliche Button-Höhe (wie Row-/DevicePanel-Buttons)
    addBtn.setBounds (top.removeFromLeft (110).withSizeKeepingCentre (106, btnH));
    manageFoldersBtn.setBounds (top.removeFromRight (200).withSizeKeepingCentre (196, btnH));   // rechtsbündig
    r.removeFromTop (4);

    if (scanLabel.isVisible())
    {
        auto row = r.removeFromTop (22);
        skipScanBtn.setBounds (row.removeFromRight (48).reduced (0, 1));
        row.removeFromRight (4);
        scanBar.setBounds (row.removeFromRight (120).reduced (0, 4));
        scanLabel.setBounds (row);
        r.removeFromTop (2);
    }
    else if (skipLabel.isVisible())
    {
        skipLabel.setBounds (r.removeFromTop (20));
        r.removeFromTop (2);
    }

    viewport.setBounds (r);

    const int vh = viewport.getHeight();
    const int contentH = rows.size() * rowH;
    // Scrollbar-Breite reservieren, wenn die Kette überläuft -> der Mülleimer am
    // rechten Rand verschwindet nie hinter dem Scrollbalken.
    const int w = viewport.getWidth() - (contentH > vh ? viewport.getScrollBarThickness() : 0);
    rowsHolder.setSize (w, juce::jmax (vh, contentH));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, w, rowH);
}

void PluginListView::rebuildRows()
{
    rows.clear();
    const int n = (int) engine.getChain().entries().size();
    for (int i = 0; i < n; ++i)
    {
        auto* row = new Row (*this, i);
        rowsHolder.addAndMakeVisible (row);
        rows.add (row);
    }
    resized();
}

void PluginListView::commitChange()
{
    engine.rebuildGraph();
    engine.requestPersist();   // App persistiert den Gesamtzustand (nicht nur captureState)
    // Vor rebuildRows: die Zeilen lesen conflictingHotkeys beim Erzeugen.
    applyHotkeyBindings();
    rebuildRows();
}

void PluginListView::applyHotkeyBindings()
{
    juce::Array<Hotkey> bindings;
    for (auto& e : engine.getChain().entries())
        bindings.add (e.hotkey);
    conflictingHotkeys = hotkeys.setBindings (bindings);
}

void PluginListView::confirmModifierlessHotkey (int index, const Hotkey& hk)
{
    // Ein Buchstabe oder eine Ziffer ohne Modifier trifft man beim normalen Tippen
    // dauernd; F-Tasten und Ziffernblock sind deutlich harmloser. Der Text sagt beides.
    const bool typedOften = (hk.key >= 'A' && hk.key <= 'Z') || (hk.key >= '0' && hk.key <= '9');

    juce::String msg;
    msg << "\"" << hk.toDisplayString() << "\" has no modifier key.\n\n"
        << "MicVST never takes a key away from other programs - it only watches for it. "
           "Without a modifier that means the plugin toggles every single time you press "
           "this key in any application";
    msg << (typedOften ? ", including while you are typing. For a letter or a digit that is "
                         "almost never what you want.\n\nUse it anyway?"
                       : ".\n\nUse it?");

    juce::NativeMessageBox::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon, "Hotkey without a modifier", msg, nullptr,
        juce::ModalCallbackFunction::create ([this, index, hk] (int result)
        {
            if (result == 1) setHotkey (index, hk);
            else             rebuildRows();   // verworfen -> Feld wieder normal zeichnen
        }));
}

void PluginListView::setHotkey (int index, const Hotkey& hk)
{
    // Verzoegert wie toggleBypass/requestRemove: der Aufruf kommt aus dem HotkeyButton,
    // und commitChange baut die Zeilen (also auch diesen Button) neu auf.
    juce::MessageManager::callAsync ([this, index, hk]
    {
        if (! juce::isPositiveAndBelow (index, (int) engine.getChain().entries().size())) return;
        engine.getChain().setHotkey (index, hk);
        commitChange();
    });
}

void PluginListView::requestRemove (int index)
{
    const auto& es = engine.getChain().entries();
    if (! juce::isPositiveAndBelow (index, (int) es.size())) return;
    const auto& e = es[(size_t) index];
    const auto name = e.isBuiltIn() ? juce::String ("Mono -> Stereo")
                                    : juce::File (e.fileOrId).getFileNameWithoutExtension();

    // Sicherheitsabfrage. Der Dialog ist asynchron -> die auslösende Row bleibt am Leben,
    // bis OK gedrückt wird (das eigentliche Entfernen passiert erst im Callback).
    juce::NativeMessageBox::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon,
        "Remove plugin",
        "Remove \"" + name + "\" from the chain?",
        nullptr,
        juce::ModalCallbackFunction::create ([this, index] (int result)
        {
            if (result == 0) return;   // cancel
            if (! juce::isPositiveAndBelow (index, (int) engine.getChain().entries().size())) return;
            editorWindows.clear();     // offene Editoren schließen (sonst Dangling -> Crash)
            engine.getChain().removePlugin (index);
            commitChange();
        }));
}

void PluginListView::requestMove (int from, int to)
{
    juce::MessageManager::callAsync ([this, from, to]
    {
        const int n = (int) engine.getChain().entries().size();
        if (from == to || ! juce::isPositiveAndBelow (from, n)) { rebuildRows(); return; }
        engine.getChain().movePlugin (from, juce::jlimit (0, n - 1, to));
        commitChange();
    });
}

void PluginListView::toggleBypass (int index)
{
    juce::MessageManager::callAsync ([this, index]
    {
        const auto& es = engine.getChain().entries();
        if (! juce::isPositiveAndBelow (index, (int) es.size())) return;
        engine.getChain().setBypass (index, ! es[(size_t) index].bypassed);
        commitChange();
    });
}

void PluginListView::showPluginPicker()
{
    // Built-ins als Pseudo-Descriptions: laufen so durch dieselbe Suche/Gruppierung.
    juce::Array<juce::PluginDescription> entries;
    juce::PluginDescription m2s;
    m2s.name = "Mono -> Stereo"; m2s.manufacturerName = "Built-in";
    m2s.fileOrIdentifier = PluginChain::monoToStereoId;
    entries.add (m2s);
    juce::PluginDescription s2m;
    s2m.name = "Stereo -> Mono"; s2m.manufacturerName = "Built-in";
    s2m.fileOrIdentifier = PluginChain::stereoToMonoId;
    entries.add (s2m);
    entries.addArray (engine.getKnownPlugins().getTypes());

    // SafePointer: die CallOutBox lebt auf dem Desktop und kann die View überleben.
    juce::Component::SafePointer<PluginListView> safe (this);
    auto picker = std::make_unique<PluginPickerComponent> (entries,
        [safe] (const juce::PluginDescription& d)
        {
            if (auto* self = safe.getComponent()) self->addFromPicker (d);
        });
    juce::CallOutBox::launchAsynchronously (std::move (picker), addBtn.getScreenBounds(), nullptr);
}

void PluginListView::addFromPicker (const juce::PluginDescription& desc)
{
    if (desc.fileOrIdentifier == PluginChain::monoToStereoId)
    {
        engine.getChain().addMonoToStereo();
        commitChange();
        return;
    }
    if (desc.fileOrIdentifier == PluginChain::stereoToMonoId)
    {
        engine.getChain().addStereoToMono();
        commitChange();
        return;
    }

    double sr = engine.getDeviceManager().getCurrentAudioDevice() != nullptr
              ? engine.getDeviceManager().getCurrentAudioDevice()->getCurrentSampleRate() : 48000.0;
    juce::String err;
    if (engine.getChain().addPlugin (engine.getFormatManager(), desc, sr, 128, err))
        commitChange();
    else
        juce::Logger::writeToLog ("Plugin-Load: " + err);
}

void PluginListView::showFolderMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Add folder...");
    m.addSeparator();

    const auto& folders = engine.getPluginFolders();
    if (folders.isEmpty())
    {
        m.addItem (2, "(no custom folders)", false /*enabled*/, false);
    }
    else
    {
        // Pro Ordner ein Untermenü mit "Remove". IDs ab 1000 = Index in der Ordnerliste.
        for (int i = 0; i < folders.size(); ++i)
        {
            juce::PopupMenu sub;
            sub.addItem (1000 + i, "Remove");
            m.addSubMenu (folders[i], sub);
        }
    }

    // Rescan auch hier anbieten (nicht nur im "+ Plugin"-Menü): auch ohne Custom-Ordner
    // muss man neu scannen können. Während eines laufenden Scans deaktiviert, weil
    // rescanAllPlugins dann bewusst nichts tut.
    constexpr int rescanItemId = 3;
    m.addSeparator();
    m.addItem (rescanItemId, "Rescan all plugins", ! engine.isScanning());

    constexpr int retryItemId = 4;
    m.addItem (retryItemId, "Retry skipped plugins",
               ! engine.isScanning() && ! engine.getSkippedPlugins().isEmpty());

    constexpr int resetItemId = 5;
    m.addSeparator();
    // Reset ist nur außerhalb eines Scans erlaubt, um zu verhindern, dass eine wartende
    // Scan-Message (Teilergebnis oder scan-finished -- beide speichern den Cache) die gerade
    // gelöschte plugin_cache.xml neu anlegt. Das ITEM wird
    // hier zur Menü-Bauzeit deaktiviert, wenn ein Scan läuft -> ist es klickbar, kann keine
    // scan-finished-Message mehr ausstehen. Startet der User über den (nicht-modalen)
    // Bestätigungsdialog dennoch einen Scan (zweite Menü-Runde), ist das unschädlich: Quit
    // zerstört den ScanCoordinator, bevor je wieder etwas den Cache speichert.
    m.addItem (resetItemId, "Reset app (clear all data)...", ! engine.isScanning());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&manageFoldersBtn),
        [this, rescanItemId, retryItemId, resetItemId] (int res)
        {
            if (res == 1) { chooseFolder(); return; }
            if (res == rescanItemId) { engine.rescanAllPlugins(); updateScanUi(); return; }
            if (res == retryItemId) { engine.retrySkippedPlugins(); updateScanUi(); return; }
            if (res == resetItemId)
            {
                juce::NativeMessageBox::showOkCancelBox (
                    juce::MessageBoxIconType::WarningIcon,
                    "Reset MicVST",
                    "Deletes all settings, the plugin cache and the plugin chain, then closes "
                    "MicVST. Start it again for a first-run setup. Continue?",
                    nullptr,
                    juce::ModalCallbackFunction::create ([this] (int result)
                    {
                        if (result != 0) engine.requestFactoryReset();   // 0 = Cancel
                    }));
                return;
            }
            if (res >= 1000)
            {
                const auto& f = engine.getPluginFolders();
                const int idx = res - 1000;
                if (juce::isPositiveAndBelow (idx, f.size()))
                {
                    engine.removePluginFolder (f[idx]);
                    engine.requestPersist();
                }
            }
        });
}

void PluginListView::chooseFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Choose a VST3 folder");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            engine.addPluginFolder (dir.getFullPathName());   // hinzufügen + rescan
            engine.requestPersist();                          // Ordner persistieren
        });
}

void PluginListView::openEditor (int row)
{
    const auto& es = engine.getChain().entries();
    if (! juce::isPositiveAndBelow (row, (int) es.size())) return;
    auto* node = engine.getGraph().getNodeForId (es[(size_t) row].node);
    if (node == nullptr) return;
    auto* proc = node->getProcessor();
    if (proc == nullptr || ! proc->hasEditor()) return;

    auto* w = new EditorWindow (proc->getName(),
                                [this] (EditorWindow* self) { editorWindows.removeObject (self); });
    w->setUsingNativeTitleBar (true);
    w->setContentOwned (proc->createEditorAndMakeActive(), true);
    w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    editorWindows.add (w);
}

void PluginListView::updateScanUi()
{
    const bool scanning = engine.isScanning();
    addBtn.setEnabled (! scanning);
    scanLabel.setVisible (scanning);
    scanBar.setVisible (scanning);
    skipScanBtn.setVisible (scanning);

    const auto& skips = engine.getSkippedPlugins();
    skipLabel.setVisible (! scanning && ! skips.isEmpty());
    if (skipLabel.isVisible())
    {
        skipLabel.setText (juce::String (skips.size()) + " plugin(s) skipped - hover for details",
                           juce::dontSendNotification);
        juce::String tip;
        for (auto& s : skips)
            tip << juce::File (s.file).getFileNameWithoutExtension() << " (" << s.reason << ")\n";
        skipLabel.setTooltip (tip.trimEnd());
    }
    resized();
}
