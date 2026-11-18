#pragma once

#include "AnalysisTypes.h"
#include "NoteTracker.h"
#include "PitchAnalyzer.h"

#include <atomic>
#include <memory>
#include <vector>

namespace g2m
{

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void setLowpass (double sr, double hz, double q);
    void setHighpass (double sr, double hz, double q);
    void reset() noexcept { z1 = z2 = 0; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Whole audio -> note events pipeline. No JUCE dependency.
class GuitarToMidiEngine
{
public:
    GuitarToMidiEngine();

    // Not real-time safe (allocates). Call from prepareToPlay.
    void prepare (double sampleRate);
    void reset();

    void setSettings (const EngineSettings& s) noexcept { settings = s; }
    const EngineSettings& getSettings() const noexcept { return settings; }

    // Real-time safe. 'out' is cleared first.
    void process (const float* input, int numSamples, NoteEventBuffer& out);
    void allNotesOff (NoteEventBuffer& out);

    double getAnalysisRate() const noexcept { return analysisRate; }

    // Values for the UI (written by the audio thread).
    std::atomic<float> displayLevelDb { -120.0f };
    std::atomic<float> displayPitch   { 0.0f };   // last detected fractional note, 0 = none
    std::atomic<int>   displayNote    { -1 };     // note currently sent, -1 = none

private:
    void analyseHop (int sampleOffset, NoteEventBuffer& out);

    EngineSettings settings;
    std::unique_ptr<IPitchAnalyzer> analyzer;
    std::unique_ptr<INoteTracker>   tracker;

    double sampleRate = 48000.0;
    double analysisRate = 12000.0;
    int decimation = 4;
    int decimCounter = 0;
    Biquad hp, lp1, lp2;

    // History of decimated samples, stored twice so any window is contiguous.
    std::vector<float> history;
    int historySize = 0;
    int writePos = 0;
    int filled = 0;

    int hopSize = 48;
    int hopCounter = 0;

    // Onset detection
    static constexpr int levelHistLen = 16;
    float levelHist[levelHistLen] {};
    int levelHistPos = 0;
    int refractory = 0;
    int sinceOnset = 0;  // decimated samples since the last attack
};

} // namespace g2m
