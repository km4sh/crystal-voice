#pragma once
#include <juce_core/juce_core.h>

namespace isolation
{
constexpr const char* workerId = "cvplugin";
constexpr size_t maxStateBytes = 48 * 1024 * 1024;
enum class Command : int { hello = 1, load, prepare, getState, setState, showEditor, hideEditor };
struct Packet
{
    Command command;
    int64_t id;
    bool success;
    juce::MemoryBlock payload;
};
inline juce::MemoryBlock pack (Command command, int64_t id, const juce::MemoryBlock& payload, bool success = true)
{
    juce::MemoryOutputStream stream;
    stream.writeInt ((int) command); stream.writeInt64 (id); stream.writeBool (success);
    stream.write (payload.getData(), payload.getSize()); return stream.getMemoryBlock();
}
inline bool unpack (const juce::MemoryBlock& bytes, Packet& packet)
{
    if (bytes.getSize() < 13 || bytes.getSize() > maxStateBytes + 65536) return false;
    juce::MemoryInputStream stream (bytes, false);
    const int command = stream.readInt();
    if (command < (int) Command::hello || command > (int) Command::hideEditor) return false;
    packet.command = (Command) command; packet.id = stream.readInt64(); packet.success = stream.readBool();
    packet.payload.replaceAll (static_cast<const char*> (bytes.getData()) + 13, bytes.getSize() - 13); return true;
}
inline juce::MemoryBlock textPayload (const juce::String& value)
{ return { value.toRawUTF8(), (size_t) value.getNumBytesAsUTF8() }; }
inline juce::String payloadText (const juce::MemoryBlock& block)
{ return juce::String::fromUTF8 (static_cast<const char*> (block.getData()), (int) block.getSize()); }
}
