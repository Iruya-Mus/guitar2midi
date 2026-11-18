// Offline test for the detection engine.
// Synthesises plucked-string notes (Karplus-Strong) and checks the MIDI output.
// Build: see tests/CMakeLists.txt  ->  ./engine_test

#include "../src/dsp/GuitarToMidiEngine.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace g2m;

struct Pluck { double start, dur; int note; float amp; };

static std::vector<float> synth (const std::vector<Pluck>& plucks, double sr, double total, float noise)
{
    std::vector<float> out ((size_t) (sr * total), 0.0f);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    for (auto& p : plucks)
    {
        const double hz = 440.0 * std::pow (2.0, (p.note - 69) / 12.0);
        // Loop delay = L + 0.5 (averaging filter) + allpass delay  ==  sr / hz
        const double total = sr / hz - 0.5;
        int L = (int) std::floor (total - 0.1);
        double frac = total - L; // in [0.1, 1.1)
        const float C = (float) ((1.0 - frac) / (1.0 + frac));
        std::vector<float> line ((size_t) L);
        for (auto& v : line) v = uni (rng);
        // soften the excitation a bit (pick tone)
        for (size_t i = 1; i < line.size(); ++i) line[i] = 0.6f * line[i] + 0.4f * line[i - 1];

        const size_t s0 = (size_t) (p.start * sr);
        const size_t len = (size_t) (p.dur * sr);
        size_t idx = 0;
        float lastOut = 0, apX1 = 0, apY1 = 0;
        for (size_t i = 0; i < len && s0 + i < out.size(); ++i)
        {
            const float y = line[idx];
            const float avg = 0.5f * (y + lastOut) * 0.998f;
            lastOut = y;
            const float ap = C * avg + apX1 - C * apY1; // first-order allpass
            apX1 = avg; apY1 = ap;
            line[idx] = ap;
            idx = (idx + 1) % line.size();
            float env = 1.0f;
            const double remaining = (double) (len - i) / sr;
            if (remaining < 0.01) env = (float) (remaining / 0.01); // damp at the end
            out[s0 + i] += 0.5f * p.amp * y * env;
        }
    }
    for (auto& v : out) v += noise * uni (rng);
    return out;
}

struct Detected { double time; int note; int vel; };

static std::vector<Detected> runEngine (const std::vector<float>& audio, double sr, int block,
                                        EngineSettings s, int& bendCount)
{
    GuitarToMidiEngine engine;
    engine.prepare (sr);
    engine.setSettings (s);
    NoteEventBuffer buf;
    std::vector<Detected> res;
    bendCount = 0;
    for (size_t pos = 0; pos < audio.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, audio.size() - pos);
        engine.process (audio.data() + pos, n, buf);
        for (int i = 0; i < buf.count; ++i)
        {
            const auto& e = buf.events[(size_t) i];
            const double t = (double) (pos + (size_t) e.sampleOffset) / sr;
            if (e.type == NoteEvent::Type::NoteOn)  res.push_back ({ t, e.note, e.velocity });
            if (e.type == NoteEvent::Type::NoteOff) res.push_back ({ t, -e.note - 1, 0 });
            if (e.type == NoteEvent::Type::PitchBend) ++bendCount;
        }
    }
    return res;
}

static const char* name (int n)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    static char buf[16];
    std::snprintf (buf, sizeof buf, "%s%d", names[n % 12], n / 12 - 1);
    return buf;
}

int main()
{
    int failures = 0;

    // A melody covering the whole neck, with gaps, legato changes and a repeated note.
    std::vector<Pluck> melody;
    const int notes[] = { 40, 45, 50, 55, 59, 64, 67, 72, 76, 79, 84, 64, 64, 52, 43, 88 };
    double t = 0.2;
    for (int i = 0; i < (int) (sizeof notes / sizeof notes[0]); ++i)
    {
        const double dur = (i % 3 == 0) ? 0.25 : 0.4;
        melody.push_back ({ t, dur, notes[i], (i % 2) ? 0.9f : 0.4f });
        t += dur + ((i % 4 == 1) ? 0.0 : 0.08); // some notes back-to-back (re-pick)
    }
    const double total = t + 0.4;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int block : { 64, 512 })
        {
            EngineSettings s;
            int bends = 0;
            auto audio = synth (melody, sr, total, 0.0005f);
            auto res = runEngine (audio, sr, block, s, bends);

            // Collect note-ons and compare with expected melody, in order.
            std::vector<Detected> ons;
            for (auto& d : res) if (d.note >= 0) ons.push_back (d);

            int matched = 0, wrong = 0;
            double latencySum = 0;
            std::string log;
            size_t k = 0;
            for (auto& p : melody)
            {
                bool found = false;
                for (; k < ons.size() && ons[k].time < p.start + p.dur; ++k)
                {
                    if (ons[k].time < p.start) { ++wrong; continue; }
                    if (ons[k].note == p.note && ! found)
                    {
                        found = true; ++matched; latencySum += ons[k].time - p.start;
                    }
                    else ++wrong;
                }
                if (! found) log += std::string (" missed:") + name (p.note);
            }
            wrong += (int) (ons.size() - k);

            // Note on/off balance
            int balance = 0;
            for (auto& d : res) balance += d.note >= 0 ? 1 : -1;

            const bool ok = matched == (int) melody.size() && wrong == 0 && balance == 0;
            std::printf ("sr=%6.0f block=%4d  matched %2d/%2zu  extra=%d  hanging=%d  avg latency %.1f ms %s%s\n",
                         sr, block, matched, melody.size(), wrong, balance,
                         matched ? 1000.0 * latencySum / matched : 0.0, ok ? "OK" : "FAIL", log.c_str());
            if (! ok)
            {
                ++failures;
                for (auto& d : res)
                    std::printf ("    %.3f %s %s v=%d\n", d.time, d.note >= 0 ? "ON " : "off",
                                 name (d.note >= 0 ? d.note : -d.note - 1), d.vel);
            }
        }
    }

    // Silence / noise only must produce nothing.
    {
        std::vector<float> noise = synth ({}, 48000.0, 2.0, 0.003f); // about -50 dBFS
        int bends = 0;
        auto res = runEngine (noise, 48000.0, 256, EngineSettings {}, bends);
        std::printf ("noise only: %zu events %s\n", res.size(), res.empty() ? "OK" : "FAIL");
        if (! res.empty()) ++failures;
    }

    // Transpose + pitch bend mode.
    {
        EngineSettings s;
        s.transpose = 12;
        s.pitchBend = true;
        int bends = 0;
        auto audio = synth ({ { 0.1, 0.5, 57, 0.8f } }, 48000.0, 1.0, 0.0005f);
        auto res = runEngine (audio, 48000.0, 128, s, bends);
        const bool ok = res.size() == 2 && res[0].note == 69;
        std::printf ("transpose+12: %s (bend messages: %d)\n", ok ? "OK" : "FAIL", bends);
        if (! ok) ++failures;
    }

    // Velocity follows playing strength.
    {
        auto soft = synth ({ { 0.1, 0.4, 60, 0.1f } }, 48000.0, 0.7, 0.0f);
        auto hard = synth ({ { 0.1, 0.4, 60, 1.0f } }, 48000.0, 0.7, 0.0f);
        int b = 0;
        auto rs = runEngine (soft, 48000.0, 128, EngineSettings {}, b);
        auto rh = runEngine (hard, 48000.0, 128, EngineSettings {}, b);
        const bool ok = ! rs.empty() && ! rh.empty() && rh[0].vel > rs[0].vel + 20;
        std::printf ("velocity soft=%d hard=%d %s\n", rs.empty() ? -1 : rs[0].vel,
                     rh.empty() ? -1 : rh[0].vel, ok ? "OK" : "FAIL");
        if (! ok) ++failures;
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
