#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>

// Sends MIDI to a system MIDI port (e.g. a loopMIDI virtual port), so that
// hosts which ignore plugin MIDI output (Ableton Live with audio effects)
// can still receive the notes on another track.
//
// The audio thread only writes into a lock-free FIFO; a background thread
// does the actual (possibly blocking) driver calls.
class MidiPortOutput : private juce::Thread
{
public:
    MidiPortOutput();
    ~MidiPortOutput() override;

    // Message thread. Empty identifier closes the port. Returns false if it could not be opened.
    bool open (const juce::String& identifier);
    void close();

    juce::String getIdentifier() const;
    juce::String getName() const;
    bool isOpen() const;

    // Audio thread, real-time safe. Messages of 1..3 bytes.
    void push (const juce::MidiMessage& m) noexcept;

private:
    void run() override;
    void drain();

    struct Packet { juce::uint8 bytes[3]; juce::uint8 size; };
    static constexpr int fifoSize = 1024;
    juce::AbstractFifo fifo { fifoSize };
    std::array<Packet, fifoSize> packets {};
    juce::WaitableEvent dataReady;

    mutable juce::CriticalSection deviceLock; // never taken by the audio thread
    std::unique_ptr<juce::MidiOutput> device;
    juce::String deviceId, deviceName;

    // Notes sent per channel, so they can be released when the port changes.
    std::array<std::array<bool, 128>, 16> sounding {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPortOutput)
};
