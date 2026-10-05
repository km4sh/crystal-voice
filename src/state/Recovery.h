#pragma once
#include "state/Persistence.h"

class RecoverySession
{
public:
    explicit RecoverySession (juce::File root = settingsDirectory()) : file (root.getChildFile ("running.marker")) {}
    bool begin()
    {
        const bool unclean = file.existsAsFile();
        tracked = file.getParentDirectory().createDirectory() && file.replaceWithText ("Crystal Voice active session\n");
        return unclean;
    }
    bool isTracked() const { return tracked; }
    bool markClean() { return ! file.existsAsFile() || file.deleteFile(); }
private:
    juce::File file;
    bool tracked = false;
};
