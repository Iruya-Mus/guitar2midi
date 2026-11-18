#pragma once

#include "AnalysisTypes.h"

namespace g2m
{

// Fixed-capacity event list, real-time safe (no allocation).
struct NoteEventBuffer
{
    static constexpr int capacity = 512;
    std::array<NoteEvent, capacity> events {};
    int count = 0;

    void clear() noexcept { count = 0; }
    void push (const NoteEvent& e) noexcept
    {
        if (count < capacity)
            events[(std::size_t) count++] = e;
    }
};

// Turns a stream of AnalysisFrames into note on/off events.
// A polyphonic tracker (one voice per string) will implement the same interface.
class INoteTracker
{
public:
    virtual ~INoteTracker() = default;
    virtual void reset() = 0;
    virtual void process (const AnalysisFrame& frame, const EngineSettings& s,
                          int sampleOffset, NoteEventBuffer& out) = 0;
    virtual void allNotesOff (int sampleOffset, NoteEventBuffer& out) = 0;

    // For the UI: currently sounding (transposed) note, or -1.
    virtual int currentNote() const noexcept = 0;
};

class MonoNoteTracker final : public INoteTracker
{
public:
    void reset() override;
    void process (const AnalysisFrame& frame, const EngineSettings& s,
                  int sampleOffset, NoteEventBuffer& out) override;
    void allNotesOff (int sampleOffset, NoteEventBuffer& out) override;
    int  currentNote() const noexcept override { return outNote; }

private:
    void startNote (int note, const EngineSettings& s, int offset, NoteEventBuffer& out);
    void stopNote (int offset, NoteEventBuffer& out);
    void sendBend (float semitones, const EngineSettings& s, int offset, NoteEventBuffer& out, bool force);
    int  velocityFor (float levelDb, const EngineSettings& s) const noexcept;

    bool  gateOpen       = false;
    int   activeNote     = -1;   // detected note (before transpose)
    int   outNote        = -1;   // note actually sent (after transpose)
    int   candidate      = -1;
    int   candidateCount = 0;
    int   framesNoPitch  = 0;
    float peakSinceOnset = -120.0f;
    int   lastBend       = 8192;
    float smoothedPitch  = 0.0f;
};

} // namespace g2m
