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

// Taugt dieser Virtual-Key als Hotkey? Erlaubt sind A-Z, 0-9, F1-F12 und der komplette
// Ziffernblock (VK_NUMPAD0..9 sowie * + , - . /). Alles andere (Pfeile, Enter, Tab, ...)
// wäre als globaler Hotkey entweder unbrauchbar oder würde die Bedienung anderer Programme
// sabotieren.
//
// Bewusst NICHT erlaubt sind die Satzzeichen der HAUPTTASTATUR (VK_OEM_PLUS & Co.): welche
// Taste das ist und wie sie heißt, hängt vom Tastaturlayout ab, eine sinnvolle Beschriftung
// wäre also nicht zu garantieren. Der Ziffernblock hat dieses Problem nicht.
//
// NumLock-Hinweis: Die ZIFFERN des Blocks melden bei ausgeschaltetem NumLock andere Codes
// (VK_END, VK_INSERT, ...), ein Hotkey auf Num5 greift dann nicht. Die Operatortasten
// + - * / sind davon nicht betroffen und funktionieren immer.
bool isAllowedHotkeyVk (int vk);

// Hotkey aus einem Virtual-Key plus Modifier-Bits (MOD_*) bauen. Das ist der Hauptweg:
// der VK wird beim Aufnehmen physisch abgefragt (siehe pressedHotkeyVk), nicht aus JUCEs
// Tastencode übersetzt. Grund: JUCE liefert für die meisten druckbaren Tasten das ZEICHEN
// statt des Virtual-Keys (juce_Windowing_windows.cpp, doKeyChar übersetzt den Scancode
// zurück in das unmodifizierte Zeichen), und Numpad-"+" kommt so als '+' an, nicht als
// VK_ADD -- ein Zeichen ist aber kein Virtual-Key und für den Vergleich im Hook unbrauchbar.
Hotkey hotkeyFromVirtualKey (int vk, int mods);

// Fallback, wenn der physische VK nicht ermittelt werden konnte: aus JUCEs Tastencode
// ableiten. Deckt zuverlässig A-Z, 0-9, F1-F12 und die Numpad-Ziffern ab (für die JUCE
// echte Extended-Codes liefert), nicht die Numpad-Operatoren.
Hotkey hotkeyFromKeyCode (int juceKeyCode, bool ctrl, bool alt, bool shift, bool win);
