#include "state/Hotkey.h"

namespace
{
    // Win32-Modifier-Bits (winuser.h), hier dupliziert statt windows.h einzubinden.
    constexpr int modAlt = 0x0001, modControl = 0x0002, modShift = 0x0004, modWin = 0x0008;

    constexpr int vkF1 = 0x70, vkF12 = 0x7B;

    // Ziffernblock: VK_NUMPAD0..9 = 0x60..0x69, danach VK_MULTIPLY, VK_ADD, VK_SEPARATOR,
    // VK_SUBTRACT, VK_DECIMAL, VK_DIVIDE = 0x6A..0x6F. Ein zusammenhaengender Block.
    constexpr int vkNumpad0 = 0x60, vkDivide = 0x6F;

    // Anzeigename einer Ziffernblock-Taste (Reihenfolge wie die VKs 0x6A..0x6F).
    juce::String numpadName (int vk)
    {
        if (vk <= 0x69) return "Num" + juce::String (vk - vkNumpad0);
        static const char* const ops[] = { "*", "+", ",", "-", ".", "/" };
        return "Num" + juce::String (ops[vk - 0x6A]);
    }

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
    else if (key >= vkNumpad0 && key <= vkDivide)
        parts.add (numpadName (key));
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

    // mods == 0 ist erlaubt. Der Aufrufer sollte den Nutzer aber darauf hinweisen, dass
    // eine Taste ohne Modifier auch beim normalen Tippen in anderen Programmen ausloest
    // (siehe Hotkey::isModifierless).
    if ((juceKeyCode & juceExtendedKeyModifier) != 0)
    {
        const int vk = juceKeyCode & 0xffff;

        if (vk >= vkF1 && vk <= vkF12)             return Hotkey { mods, vk };
        if (vk >= vkNumpad0 && vk <= vkDivide)     return Hotkey { mods, vk };

        // Alles andere Extended -- auch KeyPress::numberPadDelete, das auf dem GETEILTEN
        // VK_DELETE sitzt und sich von der normalen Entf-Taste nicht unterscheiden laesst.
        return {};
    }

    const auto upper = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) juceKeyCode);
    if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9'))
        return Hotkey { mods, (int) upper };

    return {};
}
