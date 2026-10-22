#pragma once

#include "Note.h"
#include <vector>

/** The note fields Match Reference changes: its centre and pitch contour. */
struct NotePitchState
{
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

    static NotePitchState of(const Note &note);
    /** Sets these fields on `note`, leaving its dirty flags alone. */
    void applyTo(Note &note) const;
    /** Equal within `tolerance` semitones; contours are saved to 4 decimals. */
    bool isCloseTo(const NotePitchState &other, float tolerance = 1.0e-3f) const;
};

/**
 * What Match Reference last did to a project: the settings last confirmed,
 * and the pitch of each note it changed, before and right after.
 *
 * The popup reopens with those settings, and adjusts a note starting from its
 * pitch before matching for as long as the note is still as matching left
 * it. A note edited since starts again from its current pitch. Notes are
 * found by their frames, and the memory is saved with the project.
 */
struct ReferenceMatchMemory
{
    struct Settings
    {
        float notePitch = 1.0f;
        float expression = 0.0f;
        float transpose = 0.0f;
    };

    struct Entry
    {
        int startFrame = 0;
        int endFrame = 0;
        NotePitchState before;
        NotePitchState after;
    };

    Settings settings;
    std::vector<Entry> entries;

    /** The note's pitch before matching, if matching left it as it is now. */
    const NotePitchState *pitchBeforeMatching(const Note &note) const;
};
