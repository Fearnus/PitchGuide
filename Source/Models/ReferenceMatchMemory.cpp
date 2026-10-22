#include "ReferenceMatchMemory.h"

#include <cmath>
#include <cstddef>

NoteMatchState NoteMatchState::of(const Note &note)
{
    NoteMatchState state;
    state.startFrame = note.getStartFrame();
    state.endFrame = note.getEndFrame();
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

void NoteMatchState::applyPitchTo(Note &note) const
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

bool NoteMatchState::isCloseTo(const NoteMatchState &other, float tolerance) const
{
    const auto close = [tolerance](float a, float b) { return std::abs(a - b) <= tolerance; };
    if (startFrame != other.startFrame || endFrame != other.endFrame ||
        !close(midiNote, other.midiNote) ||
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

const NoteMatchState *ReferenceMatchMemory::stateBeforeMatching(const Note &note) const
{
    for (const auto &entry : entries)
        if (entry.isFor(note))
            return entry.after.isCloseTo(NoteMatchState::of(note)) ? &entry.before : nullptr;
    return nullptr;
}
