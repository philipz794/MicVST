#include <juce_core/juce_core.h>
#include <iostream>
#include "audio/Metering.h"

// Smoke-Test: beweist, dass der UnitTestRunner läuft.
struct SmokeTest : juce::UnitTest
{
    SmokeTest() : juce::UnitTest ("Smoke") {}
    void runTest() override
    {
        beginTest ("runner works");
        expect (1 + 1 == 2);
    }
};
static SmokeTest smokeTest;

struct MeteringTest : juce::UnitTest
{
    MeteringTest() : juce::UnitTest ("Metering") {}
    void runTest() override
    {
        beginTest ("silence is zero");
        {
            juce::AudioBuffer<float> buf (2, 256);
            buf.clear();
            auto r = computeLevel (buf);
            expectWithinAbsoluteError (r.rms,  0.0f, 1.0e-6f);
            expectWithinAbsoluteError (r.peak, 0.0f, 1.0e-6f);
        }

        beginTest ("DC at 1.0 -> rms 1, peak 1");
        {
            juce::AudioBuffer<float> buf (1, 128);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (0, i, 1.0f);
            auto r = computeLevel (buf);
            expectWithinAbsoluteError (r.rms,  1.0f, 1.0e-4f);
            expectWithinAbsoluteError (r.peak, 1.0f, 1.0e-4f);
        }

        beginTest ("peak picks max abs across channels");
        {
            juce::AudioBuffer<float> buf (2, 64);
            buf.clear();
            buf.setSample (0, 10,  0.25f);
            buf.setSample (1, 20, -0.80f);
            auto r = computeLevel (buf);
            expectWithinAbsoluteError (r.peak, 0.80f, 1.0e-4f);
        }
    }
};
static MeteringTest meteringTest;

#include "audio/GraphConnections.h"

struct GraphConnectionsTest : juce::UnitTest
{
    GraphConnectionsTest() : juce::UnitTest ("GraphConnections") {}
    void runTest() override
    {
        const NodeID in   { 1 };
        const NodeID out  { 2 };
        const NodeID p1   { 10 };
        const NodeID p2   { 11 };

        beginTest ("empty mono chain: input(1out) -> output(2in) = 1 Kanal");
        {
            auto c = computeChainConnections ({ { in, 0, 1 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 1);
            expect (c[0] == ChannelConnection { in, 0, out, 0 });
        }

        beginTest ("empty stereo chain: input(2out) -> output(2in) = 2 Kanäle");
        {
            auto c = computeChainConnections ({ { in, 0, 2 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 2);
            expect (c[0] == ChannelConnection { in, 0, out, 0 });
            expect (c[1] == ChannelConnection { in, 1, out, 1 });
        }

        beginTest ("mono plugin then mono->stereo converter then stereo out");
        {
            // input(1) -> p1 mono(1/1) -> p2 mono->stereo(1/2) -> output(2)
            auto c = computeChainConnections ({ { in, 0, 1 }, { p1, 1, 1 },
                                                { p2, 1, 2 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 4);
            expect (c[0] == ChannelConnection { in, 0, p1, 0 });   // input->p1 (1 Kanal)
            expect (c[1] == ChannelConnection { p1, 0, p2, 0 });   // p1->converter (1 Kanal)
            expect (c[2] == ChannelConnection { p2, 0, out, 0 });  // converter->out (2 Kanäle)
            expect (c[3] == ChannelConnection { p2, 1, out, 1 });
        }

        beginTest ("two stereo plugins: in(2)->p1(2/2)->p2(2/2)->out(2)");
        {
            auto c = computeChainConnections ({ { in, 0, 2 }, { p1, 2, 2 },
                                                { p2, 2, 2 }, { out, 2, 0 } });
            expectEquals ((int) c.size(), 6);
            expect (c[0] == ChannelConnection { in, 0, p1, 0 });
            expect (c[1] == ChannelConnection { in, 1, p1, 1 });
            expect (c[2] == ChannelConnection { p1, 0, p2, 0 });
            expect (c[3] == ChannelConnection { p1, 1, p2, 1 });
            expect (c[4] == ChannelConnection { p2, 0, out, 0 });
            expect (c[5] == ChannelConnection { p2, 1, out, 1 });
        }
    }
};
static GraphConnectionsTest graphConnectionsTest;

#include "state/Persistence.h"

struct PersistenceTest : juce::UnitTest
{
    PersistenceTest() : juce::UnitTest ("Persistence") {}
    void runTest() override
    {
        beginTest ("round-trip preserves fields + plugin blob");
        MicVSTState s;
        s.inputDevice = "Microphone"; s.outputDevice = "CABLE Output";
        s.sampleRate = 48000.0; s.bufferSize = 128;
        s.pluginFolders.add ("C:/My VST3");
        s.windowState = "0 0 600 480";
        s.updateCheckEnabled = true;
        s.updateCheckAsked = true;
        s.lastNotifiedVersion = "1.0.3";

        PluginEntryState p;
        p.fileOrId = "C:/VST3/Pro-Q 3.vst3";
        p.bypassed = true;
        const char raw[] = { 1, 2, 3, 4, 5 };
        p.state.append (raw, sizeof (raw));
        s.plugins.add (p);

        auto tree = toValueTree (s);
        auto r = fromValueTree (tree);

        expectEquals (r.inputDevice, s.inputDevice);
        expectEquals (r.outputDevice, s.outputDevice);
        expectEquals (r.sampleRate, 48000.0);
        expectEquals (r.bufferSize, 128);
        expectEquals (r.pluginFolders.size(), 1);
        expectEquals (r.pluginFolders[0], juce::String ("C:/My VST3"));
        expectEquals (r.windowState, juce::String ("0 0 600 480"));
        expect (r.updateCheckEnabled == true);
        expect (r.updateCheckAsked == true);
        expectEquals (r.lastNotifiedVersion, juce::String ("1.0.3"));
        expectEquals (r.plugins.size(), 1);
        expectEquals (r.plugins[0].fileOrId, p.fileOrId);
        expect (r.plugins[0].bypassed == true);
        expect (r.plugins[0].state == p.state);
    }
};
static PersistenceTest persistenceTest;

// JUCEs Logger landet auf Windows in OutputDebugString und ist damit weder im Terminal
// noch im CI-Log zu sehen -- ein fehlgeschlagener Test meldete sich nur über den Exit-Code.
// Darum die Meldungen zusätzlich nach stdout.
struct PrintingTestRunner : juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};

#include "state/Hotkey.h"

struct HotkeyTest : juce::UnitTest
{
    HotkeyTest() : juce::UnitTest ("Hotkey") {}
    void runTest() override
    {
        // Win32-Bits und -Keycodes, gegen die wir pruefen (winuser.h).
        constexpr int modAlt = 1, modCtrl = 2, modShift = 4, modWin = 8;
        constexpr int vkF1 = 0x70, vkF5 = 0x74, vkF12 = 0x7B;
        // JUCE auf Windows: Sondertasten sind VK | 0x10000 (juce_Windowing_windows.cpp).
        constexpr int ext = 0x10000;

        beginTest ("letters and digits map straight onto their virtual key codes");
        {
            auto k = hotkeyFromKeyCode ('K', true, false, false, false);
            expect (k.isSet());
            expectEquals (k.mods, modCtrl);
            expectEquals (k.key, (int) 'K');
            expectEquals (k.toDisplayString(), juce::String ("Ctrl+K"));

            // Kleinbuchstabe muss denselben VK ergeben -- VK_A ist 'A', nicht 'a'.
            expectEquals (hotkeyFromKeyCode ('k', true, false, false, false).key, (int) 'K');
            expectEquals (hotkeyFromKeyCode ('7', false, true, false, false).key, (int) '7');
        }

        beginTest ("function keys are translated out of JUCEs extended key range");
        {
            auto k = hotkeyFromKeyCode (vkF5 | ext, false, true, true, false);
            expect (k.isSet());
            expectEquals (k.key, vkF5);
            expectEquals (k.mods, modAlt | modShift);
            expectEquals (k.toDisplayString(), juce::String ("Alt+Shift+F5"));

            expectEquals (hotkeyFromKeyCode (vkF1  | ext, true, false, false, false).key, vkF1);
            expectEquals (hotkeyFromKeyCode (vkF12 | ext, true, false, false, false).key, vkF12);
        }

        beginTest ("keys without a modifier are allowed but flagged for the hook path");
        {
            // Ohne Modifier kann RegisterHotKey nicht benutzt werden -- es wuerde die
            // nackte Taste systemweit verschlucken. Solche Hotkeys laufen stattdessen
            // ueber den beobachtenden Low-Level-Hook, und genau das muss needsKeyboardHook
            // melden, damit GlobalHotkeys den richtigen Mechanismus waehlt.
            auto bare = hotkeyFromKeyCode (vkF5 | ext, false, false, false, false);
            expect (bare.isSet());
            expectEquals (bare.mods, 0);
            expectEquals (bare.key, vkF5);
            expect (bare.isModifierless());
            expectEquals (bare.toDisplayString(), juce::String ("F5"));

            auto bareLetter = hotkeyFromKeyCode ('K', false, false, false, false);
            expect (bareLetter.isSet());
            expect (bareLetter.isModifierless());
            expectEquals (bareLetter.toDisplayString(), juce::String ("K"));

            // Mit Modifier laeuft es ueber RegisterHotKey, der Hook ist dann unnoetig.
            expect (! hotkeyFromKeyCode ('K', true, false, false, false).isModifierless());
            expect (! hotkeyFromKeyCode (vkF5 | ext, false, false, true, false).isModifierless());

            // Ein ungesetzter Hotkey braucht gar nichts.
            expect (! Hotkey {}.isModifierless());
        }

        beginTest ("numpad keys come in as virtual keys, including the operators");
        {
            // Der Hauptweg: der VK wird beim Aufnehmen physisch abgefragt. Genau das ist
            // noetig, weil JUCE Numpad-"+" als Zeichen '+' meldet und nicht als VK_ADD --
            // ueber hotkeyFromKeyCode waere die Taste also nicht zu bekommen.
            constexpr int vkNum0 = 0x60, vkNum5 = 0x65, vkMultiply = 0x6A, vkAdd = 0x6B,
                          vkSeparator = 0x6C, vkSubtract = 0x6D, vkDecimal = 0x6E, vkDivide = 0x6F;

            auto plus = hotkeyFromVirtualKey (vkAdd, modCtrl);
            expect (plus.isSet());
            expectEquals (plus.key, vkAdd);
            expectEquals (plus.toDisplayString(), juce::String ("Ctrl+Num+"));

            expectEquals (hotkeyFromVirtualKey (vkSubtract,  modCtrl).toDisplayString(), juce::String ("Ctrl+Num-"));
            expectEquals (hotkeyFromVirtualKey (vkMultiply,  modCtrl).toDisplayString(), juce::String ("Ctrl+Num*"));
            expectEquals (hotkeyFromVirtualKey (vkDivide,    modCtrl).toDisplayString(), juce::String ("Ctrl+Num/"));
            expectEquals (hotkeyFromVirtualKey (vkSeparator, modCtrl).toDisplayString(), juce::String ("Ctrl+Num,"));
            expectEquals (hotkeyFromVirtualKey (vkDecimal,   modCtrl).toDisplayString(), juce::String ("Ctrl+Num."));
            expectEquals (hotkeyFromVirtualKey (vkNum0,      modCtrl).toDisplayString(), juce::String ("Ctrl+Num0"));
            expectEquals (hotkeyFromVirtualKey (vkNum5,      modAlt ).toDisplayString(), juce::String ("Alt+Num5"));

            // Ohne Modifier ebenfalls, und per Hook-Pfad markiert.
            auto bare = hotkeyFromVirtualKey (vkAdd, 0);
            expect (bare.isSet());
            expect (bare.isModifierless());
            expectEquals (bare.toDisplayString(), juce::String ("Num+"));
        }

        beginTest ("shift alone is a valid modifier");
        {
            // Erste Fassung erkannte die Windows-Taste ueber isCommandDown() && !isCtrlDown();
            // auf Windows IST commandModifier aber ctrlModifier, der Ausdruck war immer
            // falsch. Modifier werden jetzt direkt abgefragt -- hier der Vertrag dafuer.
            auto k = hotkeyFromVirtualKey ('K', modShift);
            expect (k.isSet());
            expectEquals (k.mods, modShift);
            expect (! k.isModifierless());
            expectEquals (k.toDisplayString(), juce::String ("Shift+K"));

            expectEquals (hotkeyFromVirtualKey (vkF5, modShift).toDisplayString(),
                          juce::String ("Shift+F5"));
            expectEquals (hotkeyFromVirtualKey (0x6B, modShift).toDisplayString(),
                          juce::String ("Shift+Num+"));
            expectEquals (hotkeyFromVirtualKey ('K', modWin).toDisplayString(),
                          juce::String ("Win+K"));
        }

        beginTest ("the allowed virtual keys are exactly letters, digits, F1-F12 and the numpad");
        {
            expect (isAllowedHotkeyVk ('A'));  expect (isAllowedHotkeyVk ('Z'));
            expect (isAllowedHotkeyVk ('0'));  expect (isAllowedHotkeyVk ('9'));
            expect (isAllowedHotkeyVk (0x70)); expect (isAllowedHotkeyVk (0x7B));
            expect (isAllowedHotkeyVk (0x60)); expect (isAllowedHotkeyVk (0x6F));

            // 0x6F (VK_DIVIDE) grenzt lueckenlos an 0x70 (VK_F1) -- dazwischen ist nichts
            // zu pruefen, der Block geht direkt in die F-Tasten ueber.
            expect (! isAllowedHotkeyVk (0x7C));   // VK_F13
            expect (! isAllowedHotkeyVk (0x5F));   // direkt unter VK_NUMPAD0
            expect (! isAllowedHotkeyVk (0xBB));   // VK_OEM_PLUS (Haupttastatur)
            expect (! isAllowedHotkeyVk (0x0D));   // VK_RETURN
            expect (! isAllowedHotkeyVk (0x2E));   // VK_DELETE
            expect (! isAllowedHotkeyVk (0x01));   // VK_LBUTTON -- Maus
            expect (! hotkeyFromVirtualKey (0xBB, modCtrl).isSet());
        }

        beginTest ("unsupported keys are rejected");
        {
            constexpr int vkReturn = 0x0D, vkLeft = 0x25, vkF13 = 0x7C, vkDelete = 0x2E,
                          vkOemNecEqual = 0x92;
            expect (! hotkeyFromKeyCode (vkReturn | ext, true, false, false, false).isSet());
            expect (! hotkeyFromKeyCode (vkLeft   | ext, true, false, false, false).isSet());
            expect (! hotkeyFromKeyCode (vkF13    | ext, true, false, false, false).isSet());
            // KeyPress::numberPadDelete sitzt auf dem GETEILTEN VK_DELETE und liesse sich
            // nicht von der normalen Entf-Taste unterscheiden.
            expect (! hotkeyFromKeyCode (vkDelete | ext, true, false, false, false).isSet());
            // KeyPress::numberPadEquals ist eine NEC-Exotik ausserhalb des Blocks.
            expect (! hotkeyFromKeyCode (vkOemNecEqual | ext, true, false, false, false).isSet());
        }

        beginTest ("punctuation on the MAIN keyboard is rejected, not silently mismapped");
        {
            // JUCE liefert hier das ZEICHEN, und ein Zeichen ist kein Virtual-Key: '+' ist
            // 0x2B, als VK waere das VK_EXECUTE. Wuerden wir das durchlassen, registrierte
            // MicVST eine voellig andere Taste als die gedrueckte.
            expect (! hotkeyFromKeyCode ('+', true, false, false, false).isSet());
            expect (! hotkeyFromKeyCode ('-', true, false, false, false).isSet());
            expect (! hotkeyFromKeyCode ('.', true, false, false, false).isSet());
            expect (! hotkeyFromKeyCode ('#', true, false, false, false).isSet());
        }

        beginTest ("all four modifiers survive the round trip");
        {
            auto k = hotkeyFromKeyCode ('A', true, true, true, true);
            expectEquals (k.mods, modCtrl | modAlt | modShift | modWin);
            expectEquals (k.toDisplayString(), juce::String ("Ctrl+Alt+Shift+Win+A"));
        }

        beginTest ("default hotkey is unset and renders as nothing");
        {
            Hotkey none;
            expect (! none.isSet());
            expect (none.toDisplayString().isEmpty());
            expect (none == Hotkey {});
        }
    }
};
static HotkeyTest hotkeyTest;

int main (int, char**)
{
    PrintingTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    if (failures > 0)
    {
        juce::Logger::writeToLog ("TESTS FAILED: " + juce::String (failures));
        return 1;
    }
    juce::Logger::writeToLog ("All tests completed successfully");
    return 0;
}
