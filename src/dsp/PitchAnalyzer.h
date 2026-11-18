#pragma once

#include "AnalysisTypes.h"
#include <vector>

namespace g2m
{

// Interface for anything that turns a window of audio into pitch estimates.
// Monophonic YIN is the first implementation; a polyphonic analyzer
// (e.g. spectral / neural) can implement the same interface later.
class IPitchAnalyzer
{
public:
    virtual ~IPitchAnalyzer() = default;

    // Called from prepareToPlay. May allocate.
    virtual void prepare (double analysisRate, int maxWindowSize) = 0;

    // Number of most recent samples the analyzer wants for the current settings.
    // Must be <= maxWindowSize passed to prepare. Must not allocate.
    virtual int requiredWindow (const EngineSettings& s) const = 0;

    // Analyse 'window' (oldest sample first) and add pitches to 'frame'.
    // Real-time safe: no allocation, no locks.
    virtual void analyze (const float* window, int windowSize,
                          const EngineSettings& s, AnalysisFrame& frame) = 0;
};

// Monophonic pitch detector, YIN algorithm (de Cheveigné & Kawahara, 2002).
class YinPitchAnalyzer final : public IPitchAnalyzer
{
public:
    void prepare (double analysisRate, int maxWindowSize) override;
    int  requiredWindow (const EngineSettings& s) const override;
    void analyze (const float* window, int windowSize,
                  const EngineSettings& s, AnalysisFrame& frame) override;

    static float noteToHz (float note) noexcept;
    static float hzToNote (float hz) noexcept;

private:
    void tauRange (const EngineSettings& s, int& minTau, int& maxTau) const noexcept;

    double rate = 12000.0;
    std::vector<float> diff; // difference function, size maxTau + 2
};

} // namespace g2m
