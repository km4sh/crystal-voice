#include "state/AutostartRegistry.h"
#include "state/Persistence.h"

namespace
{
    const juce::String keyPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\CrystalVoice";
}

namespace AutostartRegistry
{
    bool isEnabled()
    {
        return juce::WindowsRegistry::valueExists (keyPath);
    }

    void setEnabled (bool shouldRun)
    {
        const auto defaultFolder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CrystalVoice");
        if (settingsDirectory() != defaultFolder) return;
        if (shouldRun)
        {
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                          .getFullPathName();
            juce::WindowsRegistry::setValue (keyPath, "\"" + exe + "\" --tray");
        }
        else
        {
            juce::WindowsRegistry::deleteValue (keyPath);
        }
    }
}
