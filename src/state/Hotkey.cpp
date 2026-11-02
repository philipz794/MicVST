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

bool isAllowedHotkeyVk (int vk)
{
    if (vk >= 'A' && vk <= 'Z')             return true;   // VK_A..VK_Z
    if (vk >= '0' && vk <= '9')             return true;   // VK_0..VK_9
    if (vk >= vkF1 && vk <= vkF12)          return true;
    if (vk >= vkNumpad0 && vk <= vkDivide)  return true;
    return false;
}

Hotkey hotkeyFromVirtualKey (int vk, int mods)
{
    // mods == 0 ist erlaubt. Der Aufrufer sollte den Nutzer aber darauf hinweisen, dass
    // eine Taste ohne Modifier auch beim normalen Tippen in anderen Programmen ausloest
    // (siehe Hotkey::isModifierless).
    return isAllowedHotkeyVk (vk) ? Hotkey { mods, vk } : Hotkey {};
}

Hotkey hotkeyFromKeyCode (int juceKeyCode, bool ctrl, bool alt, bool shift, bool win)
{
    int mods = 0;
    if (ctrl)  mods |= modControl;
    if (alt)   mods |= modAlt;
    if (shift) mods |= modShift;
    if (win)   mods |= modWin;

    // Extended-Codes tragen den Virtual-Key direkt (F-Tasten, Numpad-Ziffern).
    if ((juceKeyCode & juceExtendedKeyModifier) != 0)
        return hotkeyFromVirtualKey (juceKeyCode & 0xffff, mods);

    // Sonst liefert JUCE ein Zeichen. Nur fuer Buchstaben und Ziffern ist das gleichzeitig
    // der Virtual-Key (VK_A ist 'A', VK_0 ist '0'); bei allem anderen -- etwa Numpad-"+",
    // das als '+' ankommt -- waere eine Umdeutung zum VK frei erfunden.
    const auto upper = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) juceKeyCode);
    if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9'))
        return hotkeyFromVirtualKey ((int) upper, mods);

    return {};
}
