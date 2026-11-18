#include "MidiPortOutput.h"

#include <cstring>

MidiPortOutput::MidiPortOutput() : juce::Thread ("Guitar2MIDI port output")
{
    startThread (juce::Thread::Priority::high);
}

MidiPortOutput::~MidiPortOutput()
{
    signalThreadShouldExit();
    dataReady.signal();
    stopThread (2000);
    close();
}

bool MidiPortOutput::open (const juce::String& identifier)
{
    close();
    if (identifier.isEmpty())
        return true;

    auto newDevice = juce::MidiOutput::openDevice (identifier);
    if (newDevice == nullptr)
        return false;

    const juce::ScopedLock sl (deviceLock);
    deviceName = newDevice->getName();
    deviceId = identifier;
    device = std::move (newDevice);
    return true;
}

void MidiPortOutput::close()
{
    const juce::ScopedLock sl (deviceLock);
    if (device != nullptr)
    {
        // Release anything still sounding so no note hangs on the receiving track.
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                if (sounding[(size_t) ch][(size_t) n])
                    device->sendMessageNow (juce::MidiMessage::noteOff (ch + 1, n));
        for (int ch = 1; ch <= 16; ++ch)
            device->sendMessageNow (juce::MidiMessage::pitchWheel (ch, 8192));
    }
    for (auto& ch : sounding)
        ch.fill (false);
    device.reset();
    deviceId.clear();
    deviceName.clear();
}

juce::String MidiPortOutput::getIdentifier() const { const juce::ScopedLock sl (deviceLock); return deviceId; }
juce::String MidiPortOutput::getName() const       { const juce::ScopedLock sl (deviceLock); return deviceName; }
bool MidiPortOutput::isOpen() const                { const juce::ScopedLock sl (deviceLock); return device != nullptr; }

void MidiPortOutput::push (const juce::MidiMessage& m) noexcept
{
    const int size = m.getRawDataSize();
    if (size < 1 || size > 3)
        return;

    const auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return; // FIFO full, drop

    auto& p = packets[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    std::memcpy (p.bytes, m.getRawData(), (size_t) size);
    p.size = (juce::uint8) size;
    dataReady.signal();
}

void MidiPortOutput::run()
{
    while (! threadShouldExit())
    {
        dataReady.wait (5);
        drain();
    }
}

void MidiPortOutput::drain()
{
    const int ready = fifo.getNumReady();
    if (ready == 0)
        return;

    const juce::ScopedLock sl (deviceLock);
    const auto scope = fifo.read (ready);

    auto sendRange = [this] (int start, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& p = packets[(size_t) (start + i)];
            juce::MidiMessage m (p.bytes, p.size);

            if (m.isNoteOn())
                sounding[(size_t) (m.getChannel() - 1)][(size_t) m.getNoteNumber()] = true;
            else if (m.isNoteOff())
                sounding[(size_t) (m.getChannel() - 1)][(size_t) m.getNoteNumber()] = false;

            if (device != nullptr)
                device->sendMessageNow (m);
        }
    };

    sendRange (scope.startIndex1, scope.blockSize1);
    sendRange (scope.startIndex2, scope.blockSize2);
}
