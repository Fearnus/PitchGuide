#pragma once

#include "../../JuceHeader.h"

#include <optional>
#include <vector>

class Project;

/**
 * What PitchGuide Live shows the singer: the notes of a reference vocal and
 * its pitch curve, in reference time (0 = the start of the reference file).
 *
 * Built from a Project that went through the editor's take analysis, so the
 * notes are exactly the ones the editor would show for the same file. Only
 * the drawing data is kept - no audio - so it is small enough to live in the
 * plug-in state and a reopened session does not analyse the song again.
 */
struct KaraokeReference
{
    struct Note
    {
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        float midi = 0.0f;
    };

    /** File the reference was analysed from (for display and re-analysis). */
    juce::String sourcePath;
    /** Non-rest notes, sorted by start time, not overlapping. */
    std::vector<Note> notes;
    /** Pitch curve as MIDI on the analysis frame grid, 0 outside notes. */
    std::vector<float> curveMidi;
    double frameSeconds = 0.0;

    bool isLoaded() const { return !notes.empty(); }
    double getDurationSeconds() const;

    /** The note sounding at a reference time, or nullptr in a gap. */
    const Note *noteAt(double referenceSeconds) const;
    /** Index of the first note ending after a reference time (notes.size()
        when none does). */
    size_t firstNoteEndingAfter(double referenceSeconds) const;
    /** Curve value at a reference time, 0 where there is none. */
    float curveAt(double referenceSeconds) const;

    /**
     * How far a sung pitch is from what the reference sings at a reference
     * time, in signed semitones (positive = sung too high), transpose applied.
     * Measured to the note or to the reference's own pitch there, whichever
     * is closer: a singer copying the reference's slides and vibrato matches
     * it as well as one holding the note. NaN where the reference has no note.
     */
    float pitchErrorAt(double referenceSeconds, float sungMidi, float transpose) const;

    /** Notes and curve of an analysed take. */
    static KaraokeReference fromProject(const Project &project,
                                        const juce::String &sourcePath);

    /** Compact binary form for the plug-in state. */
    juce::MemoryBlock toMemoryBlock() const;
    static std::optional<KaraokeReference> fromMemoryBlock(const juce::MemoryBlock &data);
};
