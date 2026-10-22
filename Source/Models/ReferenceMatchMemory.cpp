#include "ReferenceMatchMemory.h"

#include <cmath>
#include <cstddef>

NotePitchState NotePitchState::of(const Note &note)
{
    NotePitchState state;
    state.midiNote = note.getMidiNote();
    state.lastNonMacroMidiNote = note.getLastNonMacroMidiNote();
    state.tiltLeft = note.getTiltLeft();
    state.tiltRight = note.getTiltRight();
    state.vibrato = note.getVibrato();
    state.pitchDrift = note.getPitchDrift();
    state.smoothLeftFrames = note.getSmoothLeftFrames();
    state.smoothRightFrames = note.getSmoothRightFrames();
    state.deltaScale = note.getDeltaScale();
    state.deltaOffset = note.getDeltaOffset();
    state.bakedDeltaPitch = note.getBakedDeltaPitch();
    return state;
}

void NotePitchState::applyTo(Note &note) const
{
    note.setMidiNote(midiNote);
    note.setLastNonMacroMidiNote(lastNonMacroMidiNote);
    note.setTiltLeft(tiltLeft);
    note.setTiltRight(tiltRight);
    note.setVibrato(vibrato);
    note.setPitchDrift(pitchDrift);
    note.setSmoothLeftFrames(smoothLeftFrames);
    note.setSmoothRightFrames(smoothRightFrames);
    note.setDeltaScale(deltaScale);
    note.setDeltaOffset(deltaOffset);
    note.setBakedDeltaPitch(bakedDeltaPitch);
}

bool NotePitchState::isCloseTo(const NotePitchState &other, float tolerance) const
{
    const auto close = [tolerance](float a, float b) { return std::abs(a - b) <= tolerance; };
    if (!close(midiNote, other.midiNote) ||
        !close(lastNonMacroMidiNote, other.lastNonMacroMidiNote) ||
        !close(tiltLeft, other.tiltLeft) || !close(tiltRight, other.tiltRight) ||
        !close(vibrato, other.vibrato) || !close(pitchDrift, other.pitchDrift) ||
        smoothLeftFrames != other.smoothLeftFrames ||
        smoothRightFrames != other.smoothRightFrames ||
        !close(deltaScale, other.deltaScale) || !close(deltaOffset, other.deltaOffset) ||
        bakedDeltaPitch.size() != other.bakedDeltaPitch.size())
        return false;
    for (size_t i = 0; i < bakedDeltaPitch.size(); ++i)
        if (!close(bakedDeltaPitch[i], other.bakedDeltaPitch[i]))
            return false;
    return true;
}

const NotePitchState *ReferenceMatchMemory::pitchBeforeMatching(const Note &note) const
{
    for (const auto &entry : entries)
        if (entry.startFrame == note.getStartFrame() && entry.endFrame == note.getEndFrame())
            return entry.after.isCloseTo(NotePitchState::of(note)) ? &entry.before : nullptr;
    return nullptr;
}
