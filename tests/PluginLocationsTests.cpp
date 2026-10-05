#include "audio/PluginLocations.h"
#include "audio/ScanCoordinator.h"

struct PluginLocationsTests : juce::UnitTest
{
    PluginLocationsTests() : UnitTest ("Automatic plugin locations") {}
    void runTest() override
    {
       #if JUCE_WINDOWS
        beginTest ("Windows environment paths retain drive letters, spaces and quoted semicolons");
        const auto paths = pluginLocations::windowsDefaults ("D:/Apps", "D:/Shared", "E:/User", "F:/Portable",
            "D:\\Voice Effects;\"E:\\Plugins;Special\";\\\\server\\audio\\VST3");
        expect (paths.contains ("D:\\Voice Effects")); expect (paths.contains ("E:\\Plugins;Special"));
        expect (paths.contains ("\\\\server\\audio\\VST3"));
        expect (paths.contains ("E:\\User\\Programs\\Common\\VST3"));
        expect (paths.contains ("D:\\Shared\\VST3")); expect (paths.contains ("F:\\Portable\\VST3"));
        expect (paths.contains ("D:\\Apps\\VSTPlugins"));
        expect (paths.contains ("D:\\Apps\\Steinberg\\VstPlugins"));
        expect (paths.contains ("D:\\Shared\\VST2"));
        expect (paths.contains ("D:\\Shared\\Steinberg\\VST2"));

        beginTest ("duplicate spellings, trailing separators and invalid relative paths do not add scan roots");
        const auto normalised = pluginLocations::normaliseFolders ({ " C:/Plugins ", "c:\\PLUGINS\\", "C:/Plugins/.",
            "\"C:\\Plugins\"", "", "relative/plugins", "C:", "C:/Other" });
        expectEquals (normalised.size(), 2); expectEquals (normalised[0], juce::String ("C:\\Plugins"));
        expectEquals (normalised[1], juce::String ("C:\\Other"));

        beginTest ("missing base locations cannot accidentally turn into working-directory scans");
        const auto missing = pluginLocations::windowsDefaults ({}, {}, {}, {}, " ;relative; ");
        expect (missing.isEmpty());

        beginTest ("conventional mixed plugin directories discover nested VST3 bundles but not VST2 DLLs");
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("crystalvoice-locations-" + juce::Uuid().toString());
        expect (root.createDirectory().wasOk());
        const auto bundle = root.getChildFile ("Vendor/Voice.vst3"); expect (bundle.createDirectory().wasOk());
        expect (root.getChildFile ("Legacy.dll").replaceWithText ("not a VST3"));
        juce::FileSearchPath search; search.add (root); MicVST3Format format;
        const auto found = format.searchPathsForPlugins (search, true, true);
        expect (found.contains (bundle.getFullPathName()));
        expect (! found.contains (root.getChildFile ("Legacy.dll").getFullPathName()));
        expect (root.deleteRecursively());
       #endif
    }
};
static PluginLocationsTests pluginLocationsTests;
