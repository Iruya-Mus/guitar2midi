#include "PitchAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace g2m
{

float YinPitchAnalyzer::noteToHz (float note) noexcept
{
    return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
}

float YinPitchAnalyzer::hzToNote (float hz) noexcept
{
    return 69.0f + 12.0f * std::log2 (hz / 440.0f);
}

void YinPitchAnalyzer::prepare (double analysisRate, int maxWindowSize)
{
    rate = analysisRate;
    diff.assign ((size_t) maxWindowSize + 2, 0.0f);
}

void YinPitchAnalyzer::tauRange (const EngineSettings& s, int& minTau, int& maxTau) const noexcept
{
    // Allow one semitone of margin on both sides of the playable range.
    const float lowHz  = noteToHz ((float) s.lowestNote - 1.0f);
    const float highHz = noteToHz ((float) s.highestNote + 1.0f);

    const int maxAllowed = (int) diff.size() / 2 - 2; // window = 2 * maxTau
    maxTau = std::min ((int) std::ceil (rate / lowHz) + 1, maxAllowed);
    minTau = std::max (2, (int) std::floor (rate / highHz));
    minTau = std::min (minTau, maxTau - 1);
}

int YinPitchAnalyzer::requiredWindow (const EngineSettings& s) const
{
    int minTau, maxTau;
    tauRange (s, minTau, maxTau);
    return 2 * maxTau + 1; // integration window W = maxTau, plus lag up to maxTau
}

void YinPitchAnalyzer::analyze (const float* x, int windowSize,
                                const EngineSettings& s, AnalysisFrame& frame)
{
    int minTau, maxTau;
    tauRange (s, minTau, maxTau);

    // Right after a pick attack only the post-attack samples are passed in,
    // so the old note does not leak into the new one. That limits the
    // longest period we can measure: high notes are found fast, low notes
    // a few milliseconds later once enough signal has arrived.
    const bool restricted = windowSize < 2 * maxTau + 1;
    if (restricted)
    {
        maxTau = (windowSize - 1) / 2;
        if (maxTau < minTau + 2)
            return;
    }

    const int W = maxTau;

    // Use the most recent part of the window.
    x += windowSize - (W + maxTau + 1);

    // 1) Difference function d(tau)
    float* d = diff.data();
    d[0] = 0.0f;
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        float sum = 0.0f;
        const float* a = x;
        const float* b = x + tau;
        for (int j = 0; j < W; ++j)
        {
            const float delta = a[j] - b[j];
            sum += delta * delta;
        }
        d[tau] = sum;
    }

    // 2) Cumulative mean normalised difference d'(tau)
    float running = 0.0f;
    d[0] = 1.0f;
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        running += d[tau];
        d[tau] = running > 0.0f ? d[tau] * (float) tau / running : 1.0f;
    }

    // 3) Absolute threshold: first dip below threshold, then walk to its minimum.
    int best = -1;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (d[tau] < s.yinThreshold)
        {
            while (tau + 1 <= maxTau && d[tau + 1] < d[tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
        return; // no clear periodicity -> no pitch in this frame

    // With a shortened window be stricter, and reject a dip at the very edge
    // (the real period may be longer than what we can see yet).
    if (restricted && (d[best] > s.yinThreshold * 0.6f || best >= maxTau - 1))
        return;

    // 4) Parabolic interpolation around the minimum
    float refined = (float) best;
    if (best > 1 && best < maxTau + 1)
    {
        const float s0 = d[best - 1], s1 = d[best], s2 = d[best + 1];
        const float denom = s0 - 2.0f * s1 + s2;
        if (std::abs (denom) > 1.0e-9f)
            refined += 0.5f * (s0 - s2) / denom;
    }

    const float hz = (float) rate / refined;
    const float note = hzToNote (hz);
    const float confidence = std::clamp (1.0f - d[best], 0.0f, 1.0f);

    if (note >= (float) s.lowestNote - 0.5f && note <= (float) s.highestNote + 0.5f)
        frame.addPitch (note, confidence);
}

} // namespace g2m
