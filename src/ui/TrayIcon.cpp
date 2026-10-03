#include "ui/TrayIcon.h"
#include "BinaryData.h"

TrayIcon::TrayIcon()
{
    updateIcon();                 // erzeugt erst das Icon (pimpl) ...
    setIconTooltip ("Crystal Voice");
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
}

TrayIcon::~TrayIcon()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
}

void TrayIcon::updateIcon()
{
    // Dunkler Modus -> helles Icon (iconDarkMode), heller Modus -> dunkles Icon.
    const bool dark = juce::Desktop::getInstance().isDarkModeActive();
    const void* data = dark ? BinaryData::iconDarkMode_png  : BinaryData::iconLightMode_png;
    const int   size = dark ? BinaryData::iconDarkMode_pngSize : BinaryData::iconLightMode_pngSize;

    auto img = juce::ImageCache::getFromMemory (data, size);
    if (img.isValid())
        setIconImage (img, img);
}

void TrayIcon::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu m;
        m.addItem (1, "Run at Windows startup",
                   true, isAutostartOn && isAutostartOn());
        m.addSeparator();
        m.addItem (2, "Quit");
        juce::Component::SafePointer<TrayIcon> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int result)
        {
            if (auto* self = safe.getComponent())
            {
                if (result == 1 && self->setAutostart && self->isAutostartOn) self->setAutostart (! self->isAutostartOn());
                else if (result == 2 && self->onQuit) self->onQuit();
            }
        });
    }
    else if (onToggleWindow)
    {
        onToggleWindow();
    }
}
