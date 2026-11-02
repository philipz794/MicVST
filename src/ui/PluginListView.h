#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "platform/GlobalHotkeys.h"

// Plugin-Kette als Liste. Pro Zeile (von links nach rechts):
//   - Drag-Handle (Punkte) zum Umsortieren per Drag&Drop,
//   - Plugin-Name (Doppelklick öffnet den Editor),
//   - gemeldete Latenz des Plugins (getLatencySamples),
//   - Hotkey-Feld (klicken, dann Tastenkombination drücken; Esc löscht),
//   - Bypass-Schalter,
//   - Mülleimer-Icon zum Entfernen.
// Oben ein "+ Plugin"-Button (öffnet den Such-Picker; enthält auch die Built-ins).
class PluginListView : public juce::Component, private juce::Timer
{
public:
    explicit PluginListView (AudioEngine& engine);
    ~PluginListView() override;
    void resized() override;

private:
    static constexpr int rowH = 30;

    void timerCallback() override;      // hält die angezeigte Plugin-Latenz aktuell

    void showPluginPicker();            // "+ Plugin": Picker mit Suchfeld (CallOutBox)
    void addFromPicker (const juce::PluginDescription&);
    void showFolderMenu();              // Custom-VST3-Ordner verwalten (hinzufügen/entfernen)
    void chooseFolder();                // VST3-Ordner per Dialog wählen + rescannen
    void openEditor (int row);
    void rebuildRows();                 // Zeilen-Components aus der aktuellen Kette neu erzeugen
    void commitChange();                // rebuildGraph + persistieren + Zeilen aktualisieren
    void requestRemove (int index);     // verzögertes Entfernen (Row löscht sich sonst selbst)
    void requestMove (int from, int to);
    void toggleBypass (int index);
    void setHotkey (int index, const Hotkey&);   // setzt/löscht und registriert neu
    void applyHotkeyBindings();                  // Kette -> RegisterHotKey, Konflikte markieren

    // Hotkey-Feld einer Zeile. Klick schaltet in den Aufnahmemodus: die naechste
    // Tastenkombination wird uebernommen, Esc loescht, Fokusverlust bricht ab.
    struct HotkeyButton : juce::Button
    {
        explicit HotkeyButton (PluginListView& ownerIn, int idx);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusLost (FocusChangeType) override;

        PluginListView& owner;
        int index;
        bool capturing = false;
        bool conflicted = false;   // Kombination ist von einem anderen Programm belegt
    };

    // Per-Row gezeichneter Mülleimer-Button.
    struct TrashButton : juce::Button
    {
        TrashButton() : juce::Button ("Remove") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Eine Zeile: Handle + Name + Bypass + Trash. Das Handle-/Namensfeld behandelt
    // Drag (Umsortieren) und Doppelklick (Editor); Bypass/Trash sind Kind-Buttons.
    struct Row : juce::Component
    {
        Row (PluginListView& ownerIn, int idx);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool overHandle (juce::Point<int> p) const;

        PluginListView& owner;
        int index;
        juce::TextButton bypassBtn;
        HotkeyButton hotkeyBtn;
        TrashButton trashBtn;
        bool dragging = false;
        int  grabOffsetY = 0;
    };

    // Editor-Fenster, das sich beim Schließen selbst aus der Liste entfernt.
    struct EditorWindow : juce::DocumentWindow
    {
        EditorWindow (const juce::String& name, std::function<void (EditorWindow*)> onCloseIn)
            : juce::DocumentWindow (name, juce::Colours::black, juce::DocumentWindow::closeButton),
              onClose (std::move (onCloseIn)) {}
        void closeButtonPressed() override { if (onClose) onClose (this); }
        std::function<void (EditorWindow*)> onClose;
    };

    void updateScanUi();                 // Statuszeile/Skip-Hinweis + addBtn-Enable aktualisieren
    juce::Label scanLabel;               // "Scanning plugins... 12/34 - Name"
    double scanProgress = 0.0;           // 0..1, von ProgressBar per Referenz gelesen
    juce::ProgressBar scanBar { scanProgress };
    juce::TextButton skipScanBtn { "Skip" };   // überspringt das aktuell scannende Plugin
    juce::Label skipLabel;               // "N plugin(s) skipped - hover for details"

    AudioEngine& engine;
    juce::TextButton addBtn { "+ Plugin" };
    juce::TextButton manageFoldersBtn { "Manage VST3 Folders" };
    juce::Viewport viewport;
    juce::Component rowsHolder;          // Inhalt des Viewports (trägt die Zeilen)
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<EditorWindow> editorWindows;

    // Lebt so lange wie die View (und damit so lange wie die App) -- die Hotkeys muessen
    // auch greifen, wenn das Fenster versteckt und MicVST nur im Tray ist.
    GlobalHotkeys hotkeys;
    juce::Array<int> conflictingHotkeys;
};
