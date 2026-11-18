#include "NoteTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace g2m
{

void MonoNoteTracker::reset()
{
    gateOpen = false;
    activeNote = outNote = candidate = -1;
    candidateCount = framesNoPitch = 0;
    peakSinceOnset = -120.0f;
    lastBend = 8192;
    smoothedPitch = 0.0f;
}

int MonoNoteTracker::velocityFor (float levelDb, const EngineSettings& s) const noexcept
{
    // Map [gate .. -6 dBFS] to [1 .. 127]
    const float top = -6.0f;
    const float range = std::max (1.0f, top - s.gateDb);
    const float t = std::clamp ((levelDb - s.gateDb) / range, 0.0f, 1.0f);
    const float dynamic = 1.0f + t * 126.0f;
    const float v = (float) s.fixedVelocity * (1.0f - s.velocitySens) + dynamic * s.velocitySens;
    return std::clamp ((int) std::lround (v), 1, 127);
}

void MonoNoteTracker::sendBend (float semitones, const EngineSettings& s, int offset,
                                NoteEventBuffer& out, bool force)
{
    const float range = (float) std::max (1, s.bendRange);
    const int value = std::clamp ((int) std::lround (8192.0f + semitones / range * 8191.0f), 0, 16383);
    if (force || std::abs (value - lastBend) >= 24)
    {
        NoteEvent e;
        e.type = NoteEvent::Type::PitchBend;
        e.bendValue = value;
        e.sampleOffset = offset;
        out.push (e);
        lastBend = value;
    }
}

void MonoNoteTracker::startNote (int note, const EngineSettings& s, int offset, NoteEventBuffer& out)
{
    const int sent = note + s.transpose;
    if (sent < 0 || sent > 127)
        return;

    if (s.pitchBend && lastBend != 8192)
        sendBend (0.0f, s, offset, out, true); // re-centre before the new note

    NoteEvent e;
    e.type = NoteEvent::Type::NoteOn;
    e.note = sent;
    e.velocity = velocityFor (peakSinceOnset, s);
    e.sampleOffset = offset;
    out.push (e);

    activeNote = note;
    outNote = sent;
    smoothedPitch = (float) note;
}

void MonoNoteTracker::stopNote (int offset, NoteEventBuffer& out)
{
    if (outNote >= 0)
    {
        NoteEvent e;
        e.type = NoteEvent::Type::NoteOff;
        e.note = outNote;
        e.sampleOffset = offset;
        out.push (e);
    }
    activeNote = outNote = -1;
}

void MonoNoteTracker::allNotesOff (int sampleOffset, NoteEventBuffer& out)
{
    stopNote (sampleOffset, out);
    candidate = -1;
    candidateCount = 0;
}

void MonoNoteTracker::process (const AnalysisFrame& f, const EngineSettings& s,
                               int offset, NoteEventBuffer& out)
{
    // --- Gate with hysteresis --------------------------------------------
    const bool wasOpen = gateOpen;
    if (gateOpen)
        gateOpen = f.levelDb > s.gateDb - s.gateHysteresisDb;
    else
        gateOpen = f.levelDb > s.gateDb;

    if (! gateOpen)
    {
        stopNote (offset, out);
        candidate = -1;
        candidateCount = 0;
        return;
    }

    const bool newAttack = f.onset || ! wasOpen;
    if (newAttack)
    {
        peakSinceOnset = f.levelDb;
        // A new pick: the old note ends now, the new one starts once its pitch is stable.
        stopNote (offset, out);
        candidate = -1;
        candidateCount = 0;
    }
    else
    {
        peakSinceOnset = std::max (peakSinceOnset, f.levelDb);
    }

    // --- Pitch ------------------------------------------------------------
    if (f.numPitches == 0)
    {
        if (activeNote >= 0 && ++framesNoPitch > s.releaseFrames)
            stopNote (offset, out);
        return;
    }
    framesNoPitch = 0;

    // Monophonic: take the most confident estimate.
    const PitchEstimate* best = &f.pitches[0];
    for (int i = 1; i < f.numPitches; ++i)
        if (f.pitches[(size_t) i].confidence > best->confidence)
            best = &f.pitches[(size_t) i];

    const float pitch = best->midiNote;
    const int nearest = (int) std::lround (pitch);

    if (nearest == candidate)
        ++candidateCount;
    else
    {
        candidate = nearest;
        candidateCount = 1;
    }

    const bool stable = candidateCount >= std::max (1, s.stableFrames);

    if (activeNote < 0)
    {
        if (stable && nearest >= s.lowestNote && nearest <= s.highestNote)
            startNote (nearest, s, offset, out);
        return;
    }

    // A note is sounding. Decide between "same note" (maybe bent) and "new note" (legato).
    const float deviation = pitch - (float) activeNote;

    // Without pitch bend: switch when clearly closer to another semitone.
    // With pitch bend: hold the note and bend, switch only beyond the bend range.
    const float switchAt = s.pitchBend ? std::max (0.6f, (float) s.bendRange - 0.4f) : 0.6f;

    // A legato change (no new pick) needs a bit more evidence: while two notes
    // overlap the detector can briefly report a wrong (e.g. octave) pitch.
    const bool stableLegato = candidateCount >= std::max (1, s.stableFrames) + 2;

    if (stableLegato && std::abs (deviation) > switchAt && nearest >= s.lowestNote && nearest <= s.highestNote)
    {
        stopNote (offset, out);
        startNote (nearest, s, offset, out);
        return;
    }

    if (s.pitchBend)
    {
        smoothedPitch += 0.5f * (pitch - smoothedPitch);
        sendBend (smoothedPitch - (float) activeNote, s, offset, out, false);
    }
}

} // namespace g2m
