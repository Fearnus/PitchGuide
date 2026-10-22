#pragma once

#include "../Models/Project.h"
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
 * Matching always starts from given note states (bases), so previewing
 * different settings never compounds, and restoring a capture undoes it
 * exactly. The project's ReferenceMatchMemory supplies each note's state
 * before an earlier match, so settings can be changed again later.
 */
namespace ReferenceMatch
{
using Settings = ReferenceMatchMemory::Settings;

/** A note and the pitch it had, or should be matched from. */
struct NoteState
{
    Note *note = nullptr;
    NotePitchState pitch;
};

/** The notes' current pitch. */
std::vector<NoteState> capture(const std::vector<Note *> &notes);

/**
 * Where matching starts for each note: its pitch before an earlier match, if
 * the project remembers one that is still in place, else its current pitch.
 */
std::vector<NoteState> bases(const Project &project, const std::vector<Note *> &notes);

/**
 * Puts the captured states back and rebuilds the project's pitch curves.
 * Marks the notes it changes for resynthesis, and returns how many changed
 * by more than the saved precision (NotePitchState::isCloseTo).
 */
int restore(Project &project, const std::vector<NoteState> &states);

/**
 * The reference's central pitch (MIDI, transpose applied) while it sings over
 * take frames [startFrame, endFrame), or nothing if it is silent for at least
 * half of them.
 */
std::optional<float> referencePitchOver(const ReferenceTrack &reference,
                                        int startFrame, int endFrame,
                                        float transpose);

struct Outcome
{
    /** Notes the reference sings over (the others are left as their base). */
    int matched = 0;
    /** Notes whose pitch now differs from before the call by more than the
     * saved precision (NotePitchState::isCloseTo). Every note it changes at
     * all is marked for resynthesis. */
    int changed = 0;
};

/**
 * Puts each note back to its base, then matches those notes with `settings`
 * and rebuilds the project's pitch curves.
 */
Outcome apply(Project &project, const std::vector<NoteState> &bases,
              const Settings &settings);

/**
 * Records a confirmed match in the project's memory: `settings`, and for each
 * note its base and current pitch. Forgets notes that no longer exist.
 */
void remember(Project &project, const std::vector<NoteState> &bases,
              const Settings &settings);
} // namespace ReferenceMatch
