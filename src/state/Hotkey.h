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

    // Ohne Modifier: löst auch beim normalen Tippen in anderen Programmen aus, weil MicVST
    // Tasten nur beobachtet und nie verschluckt (siehe GlobalHotkeys). Bei Buchstaben und
    // Ziffern ist das fast immer unerwünscht -- die UI fragt deshalb nach und markiert
    // solche Hotkeys.
    bool isModifierless() const { return isSet() && mods == 0; }

    // Anzeigeform, z. B. "Ctrl+Shift+F5". Leer, wenn nicht gesetzt.
    juce::String toDisplayString() const;
};

// Aus einem Tastendruck einen Hotkey machen. juceKeyCode ist juce::KeyPress::getKeyCode();
// die Modifier kommen einzeln herein, damit diese Umrechnung in der Core-Library liegen
// (und getestet werden) kann, ohne juce_gui_basics zu brauchen.
//
// Modifier sind optional (siehe isModifierless). Liefert einen UNGESETZTEN Hotkey, wenn
//   - die Taste nicht unterstützt wird. Erlaubt sind A-Z, 0-9, F1-F12 und der komplette
//     Ziffernblock (Num0-Num9 sowie + - * / , .). Alles andere (Pfeile, Enter, Tab, ...)
//     wäre als globaler Hotkey entweder unbrauchbar oder würde die Bedienung anderer
//     Programme sabotieren.
//
// Bewusst NICHT erlaubt sind + - . , usw. der HAUPTTASTATUR. JUCE liefert für solche Tasten
// das Zeichen selbst, und ein Zeichen ist kein Virtual-Key: '+' ist 0x2B, was als VK
// VK_EXECUTE bedeutet -- wir würden also stillschweigend eine völlig andere Taste
// registrieren. Die richtigen Codes (VK_OEM_PLUS & Co.) hängen zudem vom Tastaturlayout ab.
// Der Ziffernblock hat dieses Problem nicht: seine VKs sind layoutunabhängig eindeutig.
//
// NumLock-Hinweis: Die Ziffern des Blocks melden bei AUSgeschaltetem NumLock andere Codes
// (VK_END, VK_INSERT, ...), ein Hotkey auf Num5 greift dann nicht. Die Operatortasten
// + - * / sind davon nicht betroffen und funktionieren immer.
Hotkey hotkeyFromKeyCode (int juceKeyCode, bool ctrl, bool alt, bool shift, bool win);
