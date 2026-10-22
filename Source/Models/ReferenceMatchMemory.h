#pragma once

#include "Note.h"
#include <vector>

/** The note fields Match Reference changes: its timing, centre and pitch
 * contour. */
struct NoteMatchState
{
    int startFrame = 0;
    int endFrame = 0;
    float midiNote = 0.0f;
    float lastNonMacroMidiNote = 0.0f;
    float tiltLeft = 0.0f;
    float tiltRight = 0.0f;
    float vibrato = 1.0f;
    float pitchDrift = 1.0f;
    int smoothLeftFrames = 0;
    int smoothRightFrames = 0;
    float deltaScale = 1.0f;
    float deltaOffset = 0.0f;
    std::vector<float> bakedDeltaPitch;

    static NoteMatchState of(const Note &note);
    /** Sets the pitch fields on `note`, leaving its timing and dirty flags
     * alone (timing changes go through applyNoteTiming()). */
    void applyPitchTo(Note &note) const;
    bool hasSameTiming(const Note &note) const
    {
        return startFrame == note.getStartFrame() && endFrame == note.getEndFrame();
    }
    /** Equal within `tolerance` semitones (timing exactly); contours are
     * saved to 4 decimals. */
    bool isCloseTo(const NoteMatchState &other, float tolerance = 1.0e-3f) const;
};

/**
 * What Match Reference last did to a project: the settings last confirmed,
 * and the state of each note it changed, before and right after.
 *
 * The popup reopens with those settings, and adjusts a note starting from its
 * state before matching for as long as the note is still as matching left
 * it. A note edited since starts again from its current state. Notes are
 * found by the take frames they were sung over (their source frames), which
 * timing changes leave alone. The memory is saved with the project.
 */
struct ReferenceMatchMemory
{
    struct Settings
    {
        float notePitch = 1.0f;
        float expression = 0.0f;
        float transpose = 0.0f;
        float timing = 0.0f;
    };

    struct Entry
    {
        int srcStartFrame = 0;
        int srcEndFrame = 0;
        NoteMatchState before;
        NoteMatchState after;

        bool isFor(const Note &note) const
        {
            return srcStartFrame == note.getSrcStartFrame() &&
                   srcEndFrame == note.getSrcEndFrame();
        }
    };

    Settings settings;
    std::vector<Entry> entries;

    /** The note's state before matching, if matching left it as it is now. */
    const NoteMatchState *stateBeforeMatching(const Note &note) const;
};
