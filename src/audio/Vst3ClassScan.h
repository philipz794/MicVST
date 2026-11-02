#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Aufzählen der Plugin-Klassen einer VST3-Datei allein über die Factory-METADATEN.
//
// Warum nicht JUCE? JUCEs findAllTypesForFile instanziiert beim Scannen JEDE Klasse der
// Datei (juce_VST3PluginFormatImpl.h, findDescriptionsSlow -> component->initialize()),
// nur um die Kanalzahlen zu erfahren. Bei Shell-Plugins ist das ruinös: WaveShell1-VST3
// 17.1 enthält 347 Klassen und braucht so gemessene 178 s (kalt) bzw. ~90 s (warm) --
// nahe am bzw. über dem Scan-Timeout, weshalb Waves-Nutzer ihre Plugins nie zu sehen
// bekamen. Reines Auslesen der Klassen-Infos derselben Datei dauert ~1 s.
//
// Was dabei verloren geht: numInputChannels/numOutputChannels und (ohne IPluginFactory2)
// version/subCategories. MicVST liest davon NICHTS -- aus einer PluginDescription werden
// nur name und manufacturerName verwendet (Picker/Suche), und die Kanalzahlen holt
// PluginChain::addPlugin ohnehin an der echten Instanz ab.

// JUCE leitet beide uid-Felder einer PluginDescription allein aus der 16-Byte-Klassen-ID
// (PClassInfo::cid) ab. Wir müssen sie BITGENAU genauso berechnen, denn beim späteren Laden
// sucht JUCE die Klasse über name + uniqueId/deprecatedUid (findClassMatchingDescription).
// Nachbau von getHashForRange/getNormalisedTUID (juce_VST3PluginFormatImpl.h:170-185)
// zusammen mit FUID::getLong1..4 im COM_COMPATIBLE-Zweig (auf Windows aktiv, fplatform.h):
// long1/long2 werden byte-vertauscht gelesen (GUID-Layout), long3/long4 big-endian.
int vst3UniqueIdFromCid (const void* cid16Bytes);

// Dasselbe für deprecatedUid, wo JUCE direkt über TUID = char[16] iteriert: char ist auf
// MSVC SIGNED, die Konvertierung nach uint32 erweitert also das Vorzeichen.
int vst3DeprecatedUidFromCid (const void* cid16Bytes);

// Klassen der Datei als PluginDescriptions anhängen (ohne Instanziierung, siehe oben).
// vst3Path darf eine einzelne .vst3-DLL oder ein Bundle-ORDNER sein; fileOrIdentifier der
// Ergebnisse ist immer vst3Path selbst.
// false = nicht verwertbar (errorOut gesetzt) -> der Aufrufer soll auf JUCEs
// findAllTypesForFile zurückfallen, das Bundles/moduleinfo.json/Sonderfälle mitbringt.
bool enumerateVst3Classes (const juce::File& vst3Path,
                           juce::Array<juce::PluginDescription>& out,
                           juce::String& errorOut);
