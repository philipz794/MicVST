// JUCE-Header BEWUSST vor <windows.h> (siehe Kommentar in Main.cpp).
#include "platform/GlobalHotkeys.h"
#include <juce_events/juce_events.h>   // MessageManager::callAsync

#if JUCE_WINDOWS
 #include <windows.h>

namespace
{
    // Der Hook-Callback bekommt keinen eigenen Kontextzeiger, also muss die Instanz
    // statisch erreichbar sein. MicVST hat genau eine (PluginListView besitzt sie).
    GlobalHotkeys* hookOwner = nullptr;

    constexpr int modAltBit = 0x0001, modCtrlBit = 0x0002, modShiftBit = 0x0004, modWinBit = 0x0008;

    bool isKeyDownNow (int vk) { return (GetAsyncKeyState (vk) & 0x8000) != 0; }

    LRESULT CALLBACK lowLevelKeyboardProc (int code, WPARAM wParam, LPARAM lParam)
    {
        // WICHTIG: immer durchreichen und nie etwas verschlucken -- dieser Hook beobachtet
        // nur. Und er muss schnell zurueckkehren, sonst bremst er die Tastatureingabe des
        // ganzen Systems; die eigentliche Arbeit laeuft daher per callAsync.
        if (code == HC_ACTION && hookOwner != nullptr)
        {
            const auto* info = (const KBDLLHOOKSTRUCT*) lParam;
            const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            const bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

            if ((isDown || isUp) && info != nullptr)
                hookOwner->handleHookedKey ((int) info->vkCode, isDown);
        }

        return CallNextHookEx (nullptr, code, wParam, lParam);
    }
}
#endif

int pressedHotkeyVk()
{
#if JUCE_WINDOWS
    // Nur die als Hotkey tauglichen Codes abfragen, nicht blind alle 256: so kann kein
    // Fremdzustand (Maustasten, Sondertasten) als Treffer durchgehen.
    for (int vk = 0x08; vk <= 0xFE; ++vk)
        if (isAllowedHotkeyVk (vk) && isKeyDownNow (vk))
            return vk;
#endif
    return 0;
}

int heldHotkeyModifiers()
{
#if JUCE_WINDOWS
    int mods = 0;
    if (isKeyDownNow (VK_CONTROL)) mods |= modCtrlBit;
    if (isKeyDownNow (VK_MENU))    mods |= modAltBit;
    if (isKeyDownNow (VK_SHIFT))   mods |= modShiftBit;
    if (isKeyDownNow (VK_LWIN) || isKeyDownNow (VK_RWIN)) mods |= modWinBit;
    return mods;
#else
    return 0;
#endif
}

GlobalHotkeys::GlobalHotkeys() = default;

GlobalHotkeys::~GlobalHotkeys()
{
    current.clear();
    updateKeyboardHook();   // entfernt den Hook, da jetzt keine Bindung mehr ihn braucht
}

juce::Array<int> GlobalHotkeys::setBindings (const juce::Array<Hotkey>& bindings)
{
    current = bindings;
    heldKeys.clear();
    updateKeyboardHook();

    juce::Array<int> failed;

#if JUCE_WINDOWS
    // Einziger Fehlerfall: der Hook liess sich nicht installieren -> kein Hotkey geht.
    if (hook == nullptr)
        for (int i = 0; i < bindings.size(); ++i)
            if (bindings[i].isSet())
                failed.add (i);
#else
    for (int i = 0; i < bindings.size(); ++i)
        if (bindings[i].isSet())
            failed.add (i);
#endif

    return failed;
}

void GlobalHotkeys::updateKeyboardHook()
{
#if JUCE_WINDOWS
    bool needed = false;
    for (auto& hk : current)
        if (hk.isSet())
            { needed = true; break; }

    if (needed && hook == nullptr)
    {
        hookOwner = this;
        hook = SetWindowsHookExW (WH_KEYBOARD_LL, lowLevelKeyboardProc,
                                  (HINSTANCE) juce::Process::getCurrentModuleInstanceHandle(), 0);
        if (hook == nullptr)
        {
            hookOwner = nullptr;
            juce::Logger::writeToLog ("Hotkeys: Tastatur-Hook konnte nicht installiert werden"
                                      " -- Hotkeys sind ohne Funktion");
        }
        else
        {
            juce::Logger::writeToLog ("Hotkeys: Tastatur-Hook aktiv");
        }
    }
    else if (! needed && hook != nullptr)
    {
        UnhookWindowsHookEx ((HHOOK) hook);
        hook = nullptr;
        hookOwner = nullptr;
        juce::Logger::writeToLog ("Hotkeys: Tastatur-Hook entfernt (kein Hotkey mehr gebunden)");
    }
#endif
}

void GlobalHotkeys::handleHookedKey (int vk, bool isDown)
{
#if JUCE_WINDOWS
    if (! isDown)
    {
        heldKeys.removeAllInstancesOf (vk);
        return;
    }

    // Tastenwiederholung: Windows schickt beim Halten laufend WM_KEYDOWN. Nur die erste
    // Flanke zaehlt, sonst wuerde ein gehaltenes "F9" das Plugin hunderte Male umschalten.
    if (heldKeys.contains (vk))
        return;

    // Modifier muessen EXAKT passen: F9 darf nicht auf Ctrl+F9 reagieren und umgekehrt.
    const int mods = heldHotkeyModifiers();

    for (int i = 0; i < current.size(); ++i)
    {
        if (! current[i].isSet() || current[i].key != vk || current[i].mods != mods)
            continue;

        heldKeys.add (vk);

        // Aus dem Hook heraus nichts Aufwendiges tun: der blockiert sonst die
        // Tastatureingabe des ganzen Systems.
        if (onHotkey)
            juce::MessageManager::callAsync ([cb = onHotkey, i] { cb (i); });
        return;
    }
#else
    juce::ignoreUnused (vk, isDown);
#endif
}
