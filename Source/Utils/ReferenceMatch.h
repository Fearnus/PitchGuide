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
 * Timing moves the note's start and end toward where the reference sings
 * them. All three are fractions, 0 (unchanged) to 1 (as the reference sings
 * it). Transpose, in semitones, is added on top of the reference's automatic
 * octave shift. Notes the reference does not sing over are left alone.
 *
 * The reference is aligned to the take as it was sung, so it is looked up
 * through each note's source frames, wherever timing edits have moved it.
 *
 * Matching always starts from given note states (bases), so previewing
 * different settings never compounds, and restoring a capture undoes it
 * exactly. The project's ReferenceMatchMemory supplies each note's state
 * before an earlier match, so settings can be changed again later.
 */
namespace ReferenceMatch
{
using Settings = ReferenceMatchMemory::Settings;

/** A note and the state it had, or should be matched from. */
struct NoteState
{
    Note *note = nullptr;
    NoteMatchState state;
};

/** The notes' current state. */
std::vector<NoteState> capture(const std::vector<Note *> &notes);

/**
 * Where matching starts for each note: its state before an earlier match, if
 * the project remembers one that is still in place, else its current state.
 */
std::vector<NoteState> bases(const Project &project, const std::vector<Note *> &notes);

/**
 * Puts the captured states back and rebuilds the project's pitch curves.
 * Marks the notes it changes for resynthesis, and returns how many changed
 * by more than the saved precision (NoteMatchState::isCloseTo).
 */
int restore(Project &project, const std::vector<NoteState> &states);

/**
 * The reference's central pitch (MIDI, transpose applied) while it sings over
 * take frames [srcStartFrame, srcEndFrame) as sung, or nothing if it is
 * silent for at least half of them.
 */
std::optional<float> referencePitchOver(const ReferenceTrack &reference,
                                        int srcStartFrame, int srcEndFrame,
                                        float transpose);

struct Outcome
{
    /** Notes the reference sings over (the others are left as their base). */
    int matched = 0;
    /** Notes whose state now differs from before the call by more than the
     * saved precision (NoteMatchState::isCloseTo). Every note it changes at
     * all is marked for resynthesis. */
    int changed = 0;
};

/**
 * Puts each note back to its base, then matches those notes with `settings`
 * and rebuilds the project's pitch curves. Timing keeps the notes in order,
 * at least a frame long, with at least a frame between notes sung with a gap,
 * and each region's unpitched audio inside the take without overlapping the
 * next region (as the timing tool does); when that is impossible, the notes
 * keep their base timing.
 */
Outcome apply(Project &project, const std::vector<NoteState> &bases,
              const Settings &settings);

/**
 * Records a confirmed match in the project's memory: `settings`, and for each
 * note its base and current state. Forgets notes that no longer exist.
 */
void remember(Project &project, const std::vector<NoteState> &bases,
              const Settings &settings);
} // namespace ReferenceMatch
