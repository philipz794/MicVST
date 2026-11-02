#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include "state/Hotkey.h"

// Systemweite Hotkeys. juce::KeyPress genuegt dafuer nicht: MicVST lebt im Tray, die
// Tastenkombination muss also auch greifen, wenn das Fenster keinen Fokus hat (und waehrend
// der Nutzer in einer DAW, einem Spiel oder Discord ist).
//
// Umgesetzt als Low-Level-Hook (WH_KEYBOARD_LL), der Tasten nur BEOBACHTET und immer
// weiterreicht -- so macht es Discord fuer Push-to-Talk auch.
//
// Ausdruecklich NICHT per RegisterHotKey, obwohl das der naheliegende Weg waere: das
// VERSCHLUCKT die Kombination systemweit. Bindet ein Nutzer damit Ctrl+C, funktioniert
// Kopieren danach nirgendwo mehr -- und zwar ohne jede Fehlermeldung, denn Ctrl+C ist von
// keinem anderen Programm global belegt (es wird pro Anwendung behandelt), die
// Registrierung gelingt also. Eine Mikrofon-Hilfsanwendung hat keinen Grund, dem System
// Tastenkombinationen wegzunehmen.
//
// Die Kehrseite des Beobachtens: ein Hotkey loest AUCH im Vordergrundprogramm aus. Wer
// Ctrl+Alt+K bindet, schickt Ctrl+Alt+K weiterhin auch an seine DAW. Fuer einen
// Bypass-Umschalter ist das der bessere Kompromiss.
//
// Der Hook wird nur installiert, wenn mindestens ein Hotkey gebunden ist. Im Default --
// nichts gebunden -- existiert er nicht. Das ist absichtlich so, weil ein globaler
// Keyboard-Hook derselbe Mechanismus ist, den Keylogger verwenden, und manche
// Virenscanner heuristisch darauf anschlagen. Er wertet ausschliesslich den
// Virtual-Key-Code gegen die gebundene Liste aus; Tastendruecke werden nicht
// gespeichert, protokolliert oder weitergegeben.
// Welcher als Hotkey taugliche Virtual-Key ist JETZT physisch gedrueckt? 0 = keiner.
// Beim Aufnehmen einer Tastenkombination gefragt, statt JUCEs Tastencode zu uebersetzen:
// JUCE liefert fuer die meisten druckbaren Tasten das Zeichen statt des Virtual-Keys, und
// Numpad-"+" kaeme so als '+' an. Der physische VK ist eindeutig und layoutunabhaengig.
int pressedHotkeyVk();

// Welche Modifier sind JETZT gedrueckt, als MOD_*-Bits. Ebenfalls direkt abgefragt, denn
// juce::ModifierKeys kennt die Windows-Taste nicht (und commandModifier ist auf Windows
// identisch mit ctrlModifier, laesst sich also nicht dafuer missbrauchen).
int heldHotkeyModifiers();

class GlobalHotkeys
{
public:
    GlobalHotkeys();
    ~GlobalHotkeys();

    // Uebernimmt die Bindungsliste (Index = Position in der Plugin-Kette); nicht gesetzte
    // Eintraege werden uebersprungen. Rueckgabe sind die Indizes, die NICHT funktionieren
    // werden -- das kann nur passieren, wenn der Hook sich nicht installieren liess (dann
    // alle). Kollisionen mit anderen Programmen gibt es hier nicht: wir beanspruchen
    // nichts, mehrere Programme koennen auf dieselbe Taste reagieren.
    juce::Array<int> setBindings (const juce::Array<Hotkey>& bindings);

    // Wird auf dem Message-Thread aufgerufen.
    std::function<void (int index)> onHotkey;

    // Nur fuer die Hook-Callback-Funktion (muss von aussen erreichbar sein).
    void handleHookedKey (int vk, bool isDown);

private:
    void updateKeyboardHook();   // installiert/entfernt den Hook je nach Bedarf

    void* hook = nullptr;        // HHOOK, ohne windows.h im Header
    juce::Array<Hotkey> current; // komplette Bindungsliste
    juce::Array<int> heldKeys;   // gerade gedrueckte gebundene VKs -> Tastenwiederholung

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalHotkeys)
};
