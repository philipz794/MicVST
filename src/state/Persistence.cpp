#include "state/Persistence.h"

namespace ids
{
    const juce::Identifier root ("MicVST"), inDev ("inputDevice"), outDev ("outputDevice"),
        sr ("sampleRate"), userBuf ("userBufferSize"), folders ("pluginFolders"), window ("windowState"),
        updEnabled ("updateCheckEnabled"), updAsked ("updateCheckAsked"), updLast ("lastNotifiedVersion"),
        plugins ("plugins"), plugin ("plugin"), fileId ("fileOrId"), byp ("bypassed"), blob ("state"),
        uid ("uniqueId");
}

juce::ValueTree toValueTree (const MicVSTState& s)
{
    juce::ValueTree t (ids::root);
    t.setProperty (ids::inDev, s.inputDevice, nullptr);
    t.setProperty (ids::outDev, s.outputDevice, nullptr);
    t.setProperty (ids::sr, s.sampleRate, nullptr);
    t.setProperty (ids::userBuf, s.bufferSize, nullptr);
    t.setProperty (ids::folders, s.pluginFolders.joinIntoString ("\n"), nullptr);
    t.setProperty (ids::window, s.windowState, nullptr);
    t.setProperty (ids::updEnabled, s.updateCheckEnabled, nullptr);
    t.setProperty (ids::updAsked, s.updateCheckAsked, nullptr);
    t.setProperty (ids::updLast, s.lastNotifiedVersion, nullptr);

    juce::ValueTree list (ids::plugins);
    for (auto& p : s.plugins)
    {
        juce::ValueTree pt (ids::plugin);
        pt.setProperty (ids::fileId, p.fileOrId, nullptr);
        pt.setProperty (ids::byp, p.bypassed, nullptr);
        pt.setProperty (ids::uid, p.uniqueId, nullptr);
        pt.setProperty (ids::blob, p.state.toBase64Encoding(), nullptr);
        list.appendChild (pt, nullptr);
    }
    t.appendChild (list, nullptr);
    return t;
}

MicVSTState fromValueTree (const juce::ValueTree& t)
{
    MicVSTState s;
    s.inputDevice  = t.getProperty (ids::inDev);
    s.outputDevice = t.getProperty (ids::outDev);
    s.sampleRate   = t.getProperty (ids::sr, 48000.0);
    // Migration v1.0.x: der alte Key "bufferSize" (immer 128) wird bewusst ignoriert --
    // im Shared-Modus war er nie wirksam. Bestandsnutzer starten mit Auto.
    s.bufferSize   = t.getProperty (ids::userBuf, 0);
    {
        const auto f = t.getProperty (ids::folders).toString();
        if (f.isNotEmpty()) { s.pluginFolders.addLines (f); s.pluginFolders.removeEmptyStrings(); }
    }
    s.windowState = t.getProperty (ids::window).toString();
    s.updateCheckEnabled  = t.getProperty (ids::updEnabled, false);
    s.updateCheckAsked    = t.getProperty (ids::updAsked, false);
    s.lastNotifiedVersion = t.getProperty (ids::updLast).toString();

    auto list = t.getChildWithName (ids::plugins);
    for (auto pt : list)
    {
        PluginEntryState p;
        p.fileOrId = pt.getProperty (ids::fileId);
        p.bypassed = pt.getProperty (ids::byp, false);
        p.uniqueId = pt.getProperty (ids::uid, 0);   // fehlt in Configs vor v1.1.2
        p.state.fromBase64Encoding (pt.getProperty (ids::blob).toString());
        s.plugins.add (p);
    }
    return s;
}

juce::File configFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("MicVST").getChildFile ("config.xml");
}

bool saveState (const MicVSTState& s)
{
    auto f = configFile();
    f.getParentDirectory().createDirectory();
    if (auto xml = toValueTree (s).createXml())
        return xml->writeTo (f);
    return false;
}

MicVSTState loadState()
{
    auto f = configFile();
    if (! f.existsAsFile()) return {};
    if (auto xml = juce::XmlDocument::parse (f))
        return fromValueTree (juce::ValueTree::fromXml (*xml));
    return {};
}
