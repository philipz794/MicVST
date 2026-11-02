#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include "state/Hotkey.h"

// Systemweite Hotkeys. juce::KeyPress genuegt dafuer NICHT: MicVST lebt im Tray, die
// Tastenkombination muss also auch greifen, wenn das Fenster keinen Fokus hat (und waehrend
// der Nutzer in einer DAW, einem Spiel oder Discord ist). Darum Win32 RegisterHotKey mit
// einem message-only Window -- kein Keyboard-Hook, der von Virenscannern angemeckert wird.
//
// setBindings() registriert die uebergebene Liste komplett neu (Index = Position in der
// Plugin-Kette). Nicht gesetzte Hotkeys werden uebersprungen, es wird also nur fuer
// tatsaechlich belegte Tasten etwas beim System angemeldet.
class GlobalHotkeys
{
public:
    GlobalHotkeys();
    ~GlobalHotkeys();

    // Gibt die Indizes zurueck, deren Registrierung fehlschlug -- praktisch immer, weil ein
    // anderes Programm die Kombination schon haelt. Der Aufrufer kann das in der UI zeigen.
    juce::Array<int> setBindings (const juce::Array<Hotkey>& bindings);

    // Wird auf dem Message-Thread aufgerufen (das Fenster gehoert ihm, und JUCEs
    // Message-Loop verteilt WM_HOTKEY per DispatchMessage an unsere WndProc).
    std::function<void (int index)> onHotkey;

private:
    void* hwnd = nullptr;        // HWND, ohne windows.h im Header
    juce::Array<int> registered; // Indizes, die aktuell beim System angemeldet sind

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalHotkeys)
};
