#pragma once
#include <juce_core/juce_core.h>

// Ein globaler Hotkey als WIN32-Werte: mods = MOD_ALT/MOD_CONTROL/MOD_SHIFT/MOD_WIN,
// key = Virtual-Key-Code. Bewusst plattformfreie ints und nur juce_core als Abhängigkeit,
// damit Persistenz und Plugin-Kette dieses Header einbinden können, ohne windows.h zu
// sehen (das pollutet nachfolgende JUCE-Header, siehe Kommentar in Main.cpp) und ohne
// juce_gui_basics in die Core-Library zu ziehen.
//
// Nicht gesetzt = key == 0. Das ist der Default für jedes Plugin: ohne ausdrücklich
// gesetzten Hotkey wird kein RegisterHotKey aufgerufen, MicVST kann also keiner DAW und
// keinem anderen Programm Tasten wegnehmen. "Kein Hotkey gesetzt" ist der Aus-Zustand --
// darum braucht es keinen zusätzlichen globalen Schalter.
struct Hotkey
{
    int mods = 0;
    int key  = 0;

    bool isSet() const { return key != 0; }
    bool operator== (const Hotkey& o) const { return mods == o.mods && key == o.key; }

    // Anzeigeform, z. B. "Ctrl+Shift+F5". Leer, wenn nicht gesetzt.
    juce::String toDisplayString() const;
};

// Aus einem Tastendruck einen Hotkey machen. juceKeyCode ist juce::KeyPress::getKeyCode();
// die Modifier kommen einzeln herein, damit diese Umrechnung in der Core-Library liegen
// (und getestet werden) kann, ohne juce_gui_basics zu brauchen.
//
// Liefert einen UNGESETZTEN Hotkey, wenn
//   - kein Modifier gedrückt ist: RegisterHotKey auf eine nackte Taste würde sie systemweit
//     abfangen, der Nutzer könnte dann z. B. kein "k" mehr tippen;
//   - die Taste nicht unterstützt wird. Erlaubt sind A-Z, 0-9 und F1-F12. Alles andere
//     (Pfeile, Enter, Tab, ...) wäre als globaler Hotkey entweder unbrauchbar oder würde
//     die Bedienung anderer Programme sabotieren.
Hotkey hotkeyFromKeyCode (int juceKeyCode, bool ctrl, bool alt, bool shift, bool win);
