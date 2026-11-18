#pragma once

// Common data types shared by the analysis stages.
// This code has no JUCE dependency so it can be unit-tested on its own.
//
// The pipeline is:
//   audio -> Preprocessor (filter + decimate) -> IPitchAnalyzer -> AnalysisFrame
//         -> INoteTracker -> NoteEvents (note on / off / pitch bend)
//
// An AnalysisFrame can carry several pitch estimates, so a polyphonic
// analyzer (chords) can later be plugged in without touching the rest.

#include <array>
#include <cstddef>
#include <cstdint>

namespace g2m
{

constexpr int kMaxVoices = 6; // one per guitar string

struct PitchEstimate
{
    float midiNote   = 0.0f; // fractional MIDI note number (69.0 == A4 440 Hz)
    float confidence = 0.0f; // 0..1
};

struct AnalysisFrame
{
    float levelDb = -120.0f; // short-term RMS level of the input, dBFS
    bool  onset   = false;   // a new pick attack was detected in this frame
    int   numPitches = 0;
    std::array<PitchEstimate, kMaxVoices> pitches {};

    void clearPitches() noexcept { numPitches = 0; }
    void addPitch (float note, float conf) noexcept
    {
        if (numPitches < kMaxVoices)
            pitches[(std::size_t) numPitches++] = { note, conf };
    }
};

struct NoteEvent
{
    enum class Type : uint8_t { NoteOn, NoteOff, PitchBend };

    Type type      = Type::NoteOn;
    int  note      = 0;    // MIDI note (NoteOn/NoteOff)
    int  velocity  = 0;    // 1..127 (NoteOn)
    int  bendValue = 8192; // 0..16383, 8192 == centre (PitchBend)
    int  sampleOffset = 0; // position inside the current audio block
};

// User-facing settings. Everything here can change at any time
// (no allocation depends on it except where noted).
struct EngineSettings
{
    float gateDb          = -45.0f; // below this level no notes are played
    float gateHysteresisDb = 6.0f;  // gate closes at gateDb - hysteresis
    float onsetDb         = 8.0f;   // level jump (dB) treated as a new pick
    float yinThreshold    = 0.15f;  // lower = stricter pitch detection
    int   lowestNote      = 40;     // E2  (standard tuning low string)
    int   highestNote     = 88;     // E6  (24th fret, high E string)
    int   stableFrames    = 2;      // frames a pitch must hold before a note is played
    int   releaseFrames   = 12;     // frames without pitch before note off
    float velocitySens    = 0.8f;   // 0 = fixed velocity, 1 = full dynamics
    int   fixedVelocity   = 100;
    bool  pitchBend       = false;  // send fine pitch deviation (vibrato/bends)
    int   bendRange       = 2;      // must match the synth's bend range (semitones)
    int   transpose       = 0;      // semitones
};

} // namespace g2m
