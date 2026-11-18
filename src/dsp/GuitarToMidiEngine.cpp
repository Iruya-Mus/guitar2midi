#include "GuitarToMidiEngine.h"

#include <algorithm>
#include <cmath>

namespace g2m
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTargetAnalysisRate = 12000.0; // enough for guitar fundamentals (< 1.4 kHz)
    constexpr double kHopSeconds = 0.004;            // analyse every 4 ms
    constexpr float  kLowestSupportedHz = 50.0f;     // ~G1, covers 7-string and drop tunings
}

void Biquad::setLowpass (double sr, double hz, double q)
{
    const double w = 2.0 * kPi * hz / sr;
    const double alpha = std::sin (w) / (2.0 * q);
    const double c = std::cos (w);
    const double a0 = 1.0 + alpha;
    b0 = (float) ((1.0 - c) / 2.0 / a0);
    b1 = (float) ((1.0 - c) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * c / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

void Biquad::setHighpass (double sr, double hz, double q)
{
    const double w = 2.0 * kPi * hz / sr;
    const double alpha = std::sin (w) / (2.0 * q);
    const double c = std::cos (w);
    const double a0 = 1.0 + alpha;
    b0 = (float) ((1.0 + c) / 2.0 / a0);
    b1 = (float) (-(1.0 + c) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * c / a0);
    a2 = (float) ((1.0 - alpha) / a0);
}

GuitarToMidiEngine::GuitarToMidiEngine()
    : analyzer (std::make_unique<YinPitchAnalyzer>()),
      tracker (std::make_unique<MonoNoteTracker>())
{
}

void GuitarToMidiEngine::prepare (double sr)
{
    sampleRate = sr;
    decimation = std::max (1, (int) std::lround (sr / kTargetAnalysisRate));
    analysisRate = sr / decimation;

    hp.setHighpass (sr, 45.0, 0.707);
    // 4th-order Butterworth low-pass (anti-alias before decimation)
    const double cutoff = std::min (2500.0, analysisRate * 0.42);
    lp1.setLowpass (sr, cutoff, 0.5412);
    lp2.setLowpass (sr, cutoff, 1.3066);

    const int maxTau = (int) std::ceil (analysisRate / kLowestSupportedHz) + 2;
    historySize = 2 * maxTau + 2;
    history.assign ((size_t) historySize * 2, 0.0f);
    analyzer->prepare (analysisRate, historySize);

    hopSize = std::max (1, (int) std::lround (analysisRate * kHopSeconds));
    reset();
}

void GuitarToMidiEngine::reset()
{
    hp.reset(); lp1.reset(); lp2.reset();
    std::fill (history.begin(), history.end(), 0.0f);
    writePos = filled = 0;
    decimCounter = hopCounter = 0;
    std::fill (std::begin (levelHist), std::end (levelHist), -120.0f);
    levelHistPos = 0;
    refractory = 0;
    sinceOnset = 0;
    tracker->reset();
    displayLevelDb = -120.0f;
    displayPitch = 0.0f;
    displayNote = -1;
}

void GuitarToMidiEngine::allNotesOff (NoteEventBuffer& out)
{
    tracker->allNotesOff (0, out);
    displayNote = -1;
}

void GuitarToMidiEngine::process (const float* input, int numSamples, NoteEventBuffer& out)
{
    out.clear();

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = lp2.process (lp1.process (hp.process (input[i])));

        if (++decimCounter < decimation)
            continue;
        decimCounter = 0;

        history[(size_t) writePos] = x;
        history[(size_t) (writePos + historySize)] = x;
        writePos = (writePos + 1) % historySize;
        filled = std::min (filled + 1, historySize);

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            analyseHop (i, out);
        }
    }
}

void GuitarToMidiEngine::analyseHop (int sampleOffset, NoteEventBuffer& out)
{
    // Contiguous window of the most recent historySize samples, oldest first.
    const float* window = history.data() + writePos;
    const int needed = std::min (analyzer->requiredWindow (settings), historySize);

    AnalysisFrame frame;

    // Level: RMS over the longest period we look for (stable for low notes).
    const int levelLen = std::max (hopSize, needed / 2);
    const float* recent = window + historySize - levelLen;
    double sum = 0.0;
    for (int j = 0; j < levelLen; ++j)
        sum += (double) recent[j] * recent[j];
    const float rms = (float) std::sqrt (sum / levelLen);
    frame.levelDb = 20.0f * std::log10 (std::max (rms, 1.0e-6f)) + 3.01f; // +3 dB: sine RMS -> peak dBFS

    // Onset: level jumped well above the recent minimum.
    float recentMin = frame.levelDb;
    for (float l : levelHist)
        recentMin = std::min (recentMin, l);
    levelHist[levelHistPos] = frame.levelDb;
    levelHistPos = (levelHistPos + 1) % levelHistLen;

    if (refractory > 0)
        --refractory;
    else if (frame.levelDb > settings.gateDb && frame.levelDb - recentMin >= settings.onsetDb)
    {
        frame.onset = true;
        refractory = levelHistLen; // ~64 ms before another onset can fire
        sinceOnset = hopSize;      // conservative: only trust samples from now on
    }

    // Only analyse audio recorded after the latest attack (see YinPitchAnalyzer).
    const int available = std::min ({ filled, sinceOnset, historySize });
    sinceOnset = std::min (sinceOnset + hopSize, historySize);

    if (available > 8 && frame.levelDb > settings.gateDb - settings.gateHysteresisDb)
        analyzer->analyze (window + historySize - available, available, settings, frame);

    tracker->process (frame, settings, sampleOffset, out);

    displayLevelDb = frame.levelDb;
    displayPitch = frame.numPitches > 0 ? frame.pitches[0].midiNote : 0.0f;
    displayNote = tracker->currentNote();
}

} // namespace g2m
