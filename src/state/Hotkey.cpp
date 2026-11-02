#include "state/Hotkey.h"

namespace
{
    // Win32-Modifier-Bits (winuser.h), hier dupliziert statt windows.h einzubinden.
    constexpr int modAlt = 0x0001, modControl = 0x0002, modShift = 0x0004, modWin = 0x0008;

    constexpr int vkF1 = 0x70, vkF12 = 0x7B;

    // Auf Windows sind JUCEs Key-Codes für Sondertasten buchstäblich
    // "VK_xxx | extendedKeyModifier" (juce_Windowing_windows.cpp:697ff), für Buchstaben und
    // Ziffern das Zeichen selbst -- und VK_A..VK_Z / VK_0..VK_9 haben genau die Werte von
    // 'A'..'Z' bzw. '0'..'9'. Die Umrechnung ist damit exakt und braucht keine Tabelle.
    constexpr int juceExtendedKeyModifier = 0x10000;
}

juce::String Hotkey::toDisplayString() const
{
    if (! isSet()) return {};

    juce::StringArray parts;
    if (mods & modControl) parts.add ("Ctrl");
    if (mods & modAlt)     parts.add ("Alt");
    if (mods & modShift)   parts.add ("Shift");
    if (mods & modWin)     parts.add ("Win");

    if (key >= vkF1 && key <= vkF12)
        parts.add ("F" + juce::String (key - vkF1 + 1));
    else
        parts.add (juce::String::charToString ((juce::juce_wchar) key));

    return parts.joinIntoString ("+");
}

Hotkey hotkeyFromKeyCode (int juceKeyCode, bool ctrl, bool alt, bool shift, bool win)
{
    int mods = 0;
    if (ctrl)  mods |= modControl;
    if (alt)   mods |= modAlt;
    if (shift) mods |= modShift;
    if (win)   mods |= modWin;

    if (mods == 0) return {};

    if ((juceKeyCode & juceExtendedKeyModifier) != 0)
    {
        const int vk = juceKeyCode & 0xffff;
        return (vk >= vkF1 && vk <= vkF12) ? Hotkey { mods, vk } : Hotkey {};
    }

    const auto upper = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) juceKeyCode);
    if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9'))
        return Hotkey { mods, (int) upper };

    return {};
}
