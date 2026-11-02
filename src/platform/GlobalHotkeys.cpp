// JUCE-Header BEWUSST vor <windows.h> (siehe Kommentar in Main.cpp).
#include "platform/GlobalHotkeys.h"

#if JUCE_WINDOWS
 #include <windows.h>

namespace
{
    constexpr const wchar_t* hotkeyWindowClass = L"MicVSTHotkeyWindow";

    // Jede Instanz haengt sich selbst ans Fenster (GWLP_USERDATA), damit die WndProc den
    // Callback findet. MicVST hat nur eine Instanz, aber so bleibt es sauber.
    LRESULT CALLBACK hotkeyWndProc (HWND h, UINT msg, WPARAM w, LPARAM l)
    {
        if (msg == WM_HOTKEY)
            if (auto* self = (GlobalHotkeys*) GetWindowLongPtr (h, GWLP_USERDATA))
                if (self->onHotkey)
                    { self->onHotkey ((int) w); return 0; }

        return DefWindowProc (h, msg, w, l);
    }
}
#endif

GlobalHotkeys::GlobalHotkeys()
{
#if JUCE_WINDOWS
    static bool classRegistered = false;
    if (! classRegistered)
    {
        WNDCLASSEXW wc {};
        wc.cbSize        = sizeof (wc);
        wc.lpfnWndProc   = hotkeyWndProc;
        wc.hInstance     = (HINSTANCE) juce::Process::getCurrentModuleInstanceHandle();
        wc.lpszClassName = hotkeyWindowClass;
        classRegistered = (RegisterClassExW (&wc) != 0);
    }

    // HWND_MESSAGE: unsichtbares Fenster, das nur Nachrichten empfaengt -- taucht in keiner
    // Taskleiste und keinem Alt-Tab auf.
    hwnd = CreateWindowExW (0, hotkeyWindowClass, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr,
                            (HINSTANCE) juce::Process::getCurrentModuleInstanceHandle(), nullptr);

    if (hwnd != nullptr)
        SetWindowLongPtr ((HWND) hwnd, GWLP_USERDATA, (LONG_PTR) this);
    else
        juce::Logger::writeToLog ("Hotkeys: konnte kein Message-Window anlegen");
#endif
}

GlobalHotkeys::~GlobalHotkeys()
{
#if JUCE_WINDOWS
    if (hwnd != nullptr)
    {
        for (auto id : registered)
            UnregisterHotKey ((HWND) hwnd, id);

        // USERDATA loeschen, bevor das Fenster stirbt: eine noch in der Queue liegende
        // WM_HOTKEY darf nicht auf das halb zerstoerte Objekt zurueckrufen.
        SetWindowLongPtr ((HWND) hwnd, GWLP_USERDATA, 0);
        DestroyWindow ((HWND) hwnd);
        hwnd = nullptr;
    }
#endif
    registered.clear();
}

juce::Array<int> GlobalHotkeys::setBindings (const juce::Array<Hotkey>& bindings)
{
    juce::Array<int> failed;

#if JUCE_WINDOWS
    if (hwnd == nullptr)
    {
        for (int i = 0; i < bindings.size(); ++i)
            if (bindings[i].isSet())
                failed.add (i);
        return failed;
    }

    for (auto id : registered)
        UnregisterHotKey ((HWND) hwnd, id);
    registered.clear();

    for (int i = 0; i < bindings.size(); ++i)
    {
        const auto& hk = bindings[i];
        if (! hk.isSet())
            continue;

        // MOD_NOREPEAT: Halten der Taste soll nicht hunderte Umschaltungen ausloesen.
        // Die Hotkey-ID ist der Ketten-Index, damit WM_HOTKEY direkt die Zeile benennt.
        if (RegisterHotKey ((HWND) hwnd, i, (UINT) hk.mods | 0x4000, (UINT) hk.key))
            registered.add (i);
        else
            failed.add (i);
    }

    if (! failed.isEmpty())
        juce::Logger::writeToLog ("Hotkeys: " + juce::String (failed.size())
                                  + " Kombination(en) bereits von einem anderen Programm belegt");
#else
    for (int i = 0; i < bindings.size(); ++i)
        if (bindings[i].isSet())
            failed.add (i);
#endif

    return failed;
}
