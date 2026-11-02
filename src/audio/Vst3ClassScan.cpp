// JUCE-Header BEWUSST vor <windows.h>: umgekehrt pollutet windows.h nachfolgende
// JUCE-Header (Makrokonflikte, siehe Kommentar in Main.cpp).
#include "audio/Vst3ClassScan.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    // --- Minimale VST3-ABI-Deklarationen -------------------------------------------
    // Absichtlich kein SDK-Include: gebraucht wird nur IPluginFactory (die ersten sieben
    // vtable-Slots) und zwei POD-Structs. Damit entfällt jede Arbeit mit TUID-Konstanten
    // (queryInterface wird nie aufgerufen), und MicVST hängt nicht am SDK-Pfad, den JUCE
    // intern mitliefert. Das VST3-ABI ist stabil; __stdcall ist auf x64 ein No-op, steht
    // aber korrekt da, falls je 32-Bit gebaut wird.
    using TUID = char[16];

    struct PFactoryInfo { char vendor[64]; char url[256]; char email[128]; juce::int32 flags; };
    struct PClassInfo   { TUID cid; juce::int32 cardinality; char category[32]; char name[64]; };

    struct FUnknownAbi
    {
        virtual juce::int32  __stdcall queryInterface (const TUID iid, void** obj) = 0;
        virtual juce::uint32 __stdcall addRef() = 0;
        virtual juce::uint32 __stdcall release() = 0;
    };

    struct IPluginFactoryAbi : FUnknownAbi
    {
        virtual juce::int32 __stdcall getFactoryInfo (PFactoryInfo*) = 0;
        virtual juce::int32 __stdcall countClasses() = 0;
        virtual juce::int32 __stdcall getClassInfo (juce::int32 index, PClassInfo*) = 0;
        virtual juce::int32 __stdcall createInstance (const char* cid, const char* iid, void** obj) = 0;
    };

    using InitDllFn    = bool                (__stdcall*)();
    using ExitDllFn    = bool                (__stdcall*)();
    using GetFactoryFn = IPluginFactoryAbi*  (__stdcall*)();

    constexpr juce::int32 kResultOk = 0;

    // Kategorie, die eine Klasse als Audio-Effekt ausweist (kVstAudioEffectClass).
    // Alles andere (Controller, ARA-Factories, ...) überspringt auch JUCE.
    constexpr const char* kAudioModuleClass = "Audio Module Class";

    // Welche Binary muss geladen werden? Einzelne .vst3-DLL -> sie selbst. Bundle-ORDNER
    // -> die Binary für UNSERE Architektur. Mehrdeutige Bundles werden abgelehnt (false
    // vom Aufrufer -> JUCE-Fallback), statt zu raten.
    juce::File resolveVst3Binary (const juce::File& path)
    {
        if (! path.isDirectory())
            return path;

        const auto inner = path.findChildFiles (juce::File::findFiles, true, "*.vst3");
        if (inner.isEmpty())
            return {};

       #if JUCE_64BIT
        const char* wanted = "x86_64-win";
       #else
        const char* wanted = "x86-win";
       #endif

        for (auto& f : inner)
            if (f.getParentDirectory().getFileName().equalsIgnoreCase (wanted))
                return f;

        return inner.size() == 1 ? inner[0] : juce::File();
    }
}

int vst3UniqueIdFromCid (const void* cid16Bytes)
{
    const auto* d = static_cast<const unsigned char*> (cid16Bytes);

    auto makeLong = [] (unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4)
    {
        return ((juce::uint32) b1 << 24) | ((juce::uint32) b2 << 16)
             | ((juce::uint32) b3 <<  8) |  (juce::uint32) b4;
    };

    // FUID::getLong1..4, COM_COMPATIBLE-Variante (Windows): die ersten acht Bytes werden
    // paarweise vertauscht gelesen (GUID-Layout), die hinteren acht unverändert.
    const juce::uint32 longs[4] = { makeLong (d[3],  d[2],  d[1],  d[0]),
                                    makeLong (d[5],  d[4],  d[7],  d[6]),
                                    makeLong (d[8],  d[9],  d[10], d[11]),
                                    makeLong (d[12], d[13], d[14], d[15]) };

    juce::uint32 value = 0;
    for (auto l : longs)
        value = (value * 31) + l;
    return (int) value;
}

int vst3DeprecatedUidFromCid (const void* cid16Bytes)
{
    const auto* d = static_cast<const char*> (cid16Bytes);   // signed, siehe Header
    juce::uint32 value = 0;
    for (int i = 0; i < 16; ++i)
        value = (value * 31) + (juce::uint32) (juce::int32) d[i];
    return (int) value;
}

bool enumerateVst3Classes (const juce::File& vst3Path,
                           juce::Array<juce::PluginDescription>& out,
                           juce::String& errorOut)
{
#if ! JUCE_WINDOWS
    juce::ignoreUnused (vst3Path, out);
    errorOut = "nur unter Windows implementiert";
    return false;
#else
    const auto binary = resolveVst3Binary (vst3Path);
    if (binary == juce::File())
    {
        errorOut = "keine eindeutige VST3-Binary im Bundle";
        return false;
    }

    auto* lib = LoadLibraryW (binary.getFullPathName().toWideCharPointer());
    if (lib == nullptr)
    {
        errorOut = "LoadLibrary fehlgeschlagen (" + juce::String ((int) GetLastError()) + ")";
        return false;
    }

    // Jeder Abbruch ab hier gibt die Library wieder frei: der Aufrufer fällt dann auf JUCE
    // zurück, das die Datei erneut lädt -- eine halb initialisierte Shell soll dabei nicht
    // im Prozess hängen bleiben.
    auto bail = [&] (const juce::String& why)
    {
        errorOut = why;
        FreeLibrary (lib);
        return false;
    };

    // Exportiert das Plugin kein InitDll, ist das in Ordnung (JUCE behandelt es genauso);
    // gibt es aber false zurück, ist die DLL nicht benutzbar.
    if (auto* initDll = (InitDllFn) GetProcAddress (lib, "InitDll"))
        if (! initDll())
            return bail ("InitDll() = false");

    auto* getFactory = (GetFactoryFn) GetProcAddress (lib, "GetPluginFactory");
    if (getFactory == nullptr)
        return bail ("GetPluginFactory nicht exportiert");

    auto* factory = getFactory();
    if (factory == nullptr)
        return bail ("GetPluginFactory() = nullptr");

    PFactoryInfo fi {};
    juce::String vendor;
    if (factory->getFactoryInfo (&fi) == kResultOk)
        vendor = juce::String (juce::CharPointer_UTF8 (fi.vendor)).trim();

    const auto now = juce::Time::getCurrentTime();
    const int numClasses = (int) factory->countClasses();
    juce::StringArray foundNames;

    for (int i = 0; i < numClasses; ++i)
    {
        PClassInfo ci {};
        if (factory->getClassInfo ((juce::int32) i, &ci) != kResultOk)
            continue;
        if (std::strcmp (ci.category, kAudioModuleClass) != 0)
            continue;

        const auto name = juce::String (juce::CharPointer_UTF8 (ci.name)).trim();
        if (foundNames.contains (name, true))   // wie JUCE: Namensdubletten überspringen
            continue;
        foundNames.add (name);

        juce::PluginDescription d;
        d.fileOrIdentifier   = vst3Path.getFullPathName();
        d.lastFileModTime    = vst3Path.getLastModificationTime();
        d.lastInfoUpdateTime = now;
        d.pluginFormatName   = "VST3";
        d.manufacturerName   = vendor;
        d.name               = name;
        d.descriptiveName    = name;
        d.category           = juce::String (juce::CharPointer_UTF8 (ci.category)).trim();
        d.uniqueId           = vst3UniqueIdFromCid (ci.cid);
        d.deprecatedUid      = vst3DeprecatedUidFromCid (ci.cid);
        d.isInstrument       = d.category.containsIgnoreCase ("Instrument");

        if (d.uniqueId != 0)   // wie JUCE: eine Klasse ohne brauchbare uid ist nicht ladbar
            out.add (d);
    }

    // Ordentlich herunterfahren, BEVOR die Funktion zurückkehrt. Nötig, nicht nur sauber:
    // Lässt man die Shell einfach geladen und überlässt das Entladen dem Prozessende,
    // stirbt WaveShell mit 0xC0000409 (STATUS_STACK_BUFFER_OVERRUN) -- es will sein
    // ExitDll sehen. Ab hier darf factory nicht mehr angefasst werden; alle Daten liegen
    // schon als juce::String in out. Reihenfolge: release -> ExitDll -> FreeLibrary.
    factory->release();
    if (auto* exitDll = (ExitDllFn) GetProcAddress (lib, "ExitDll"))
        exitDll();
    FreeLibrary (lib);

    if (out.isEmpty())
    {
        errorOut = "Factory meldet " + juce::String (numClasses) + " Klasse(n), davon keine nutzbare";
        return false;
    }
    return true;
#endif
}
