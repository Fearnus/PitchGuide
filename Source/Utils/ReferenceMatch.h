#pragma once

#include "../Models/Project.h"
#include "TransformParams.h"
#include <optional>
#include <vector>

/**
 * Pulls notes toward the project's reference vocal (Project::getReference()).
 *
 * Note pitch moves each note's centre toward the reference's central pitch
 * over that note. Expression blends the note's contour toward the
 * reference's slides and vibrato, around wherever the note's centre now is.
 * Both are fractions, 0 (unchanged) to 1 (as the reference sings it).
 * Transpose, in semitones, is added on top of the reference's automatic
 * octave shift. Notes the reference does not sing over are left alone.
 *
 * Matching always starts from captured note states, so previewing different
 * settings never compounds, and restoring the capture undoes it exactly.
 */
namespace ReferenceMatch
{
struct Settings
{
    float notePitch = 1.0f;
    float expression = 0.0f;
    float transpose = 0.0f;
};

/** Everything matching changes on a note, so it can be put back. */
struct NoteState
{
    Note *note = nullptr;
    TransformParams params;
    std::vector<float> bakedDeltaPitch;
    float lastNonMacroMidiNote = 0.0f;
};

std::vector<NoteState> capture(const std::vector<Note *> &notes);

/** Puts the captured states back and rebuilds the project's pitch curves.
 * Like apply(), marks the notes' frames for resynthesis. */
void restore(Project &project, const std::vector<NoteState> &states);

/**
 * The reference's central pitch (MIDI, transpose applied) while it sings over
 * take frames [startFrame, endFrame), or nothing if it is silent for at least
 * half of them.
 */
std::optional<float> referencePitchOver(const ReferenceTrack &reference,
                                        int startFrame, int endFrame,
                                        float transpose);

/**
 * Restores `original`, then matches those notes with `settings` and rebuilds
 * the project's pitch curves. Returns how many notes the reference sings over
 * (the others are unchanged).
 */
int apply(Project &project, const std::vector<NoteState> &original,
          const Settings &settings);
} // namespace ReferenceMatch
