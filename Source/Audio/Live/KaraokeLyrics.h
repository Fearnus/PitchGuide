#pragma once

#include "../../JuceHeader.h"

#include <optional>
#include <vector>

struct KaraokeReference;

/**
 * Lyrics for PitchGuide Live, timed down to the syllable.
 *
 * The text comes from the user: LRC (a time per line, from lyrics sites such
 * as lrclib.net), enhanced LRC (a time per word or syllable as well), or
 * plain text with no times at all. What is missing is filled in from the
 * reference vocal's notes, which already say when every syllable is sung:
 *
 *  - LRC line times are snapped to the start of the phrase the reference
 *    sings there, and each line's syllables are matched to its notes (one
 *    note per syllable where possible, extra notes held as melisma, extra
 *    syllables sharing the longest notes).
 *  - Plain text is laid onto the reference's phrases by syllable count and
 *    the pauses between notes.
 *
 * All times are reference times (0 = the start of the reference file), the
 * same clock as KaraokeReference.
 */
struct KaraokeLyrics
{
    enum class SourceTiming
    {
        /** Plain text: placed on the reference's phrases. */
        None,
        /** LRC: a time per line. */
        Lines,
        /** Enhanced LRC: a time per word (or syllable) as well. */
        Words
    };

    struct Syllable
    {
        /** Character range in Line::text (punctuation stays with its syllable). */
        int charStart = 0;
        int charEnd = 0;
        /** Time written in the file for this syllable (enhanced LRC), or -1. */
        double lrcSeconds = -1.0;
        /** When it is sung, -1 while untimed. */
        double startSeconds = -1.0;
        double endSeconds = -1.0;
        /** Reference note the syllable starts on, -1 when it was not matched to one. */
        int note = -1;

        bool isTimed() const { return endSeconds > startSeconds && startSeconds >= 0.0; }
    };

    struct Word
    {
        int charStart = 0;
        int charEnd = 0;
        int firstSyllable = 0;
        int numSyllables = 0;
    };

    struct Line
    {
        /** What is shown: the line with its whitespace collapsed. */
        juce::String text;
        /** Section header written above the line ("Chorus", "Verse 2"), if any. */
        juce::String section;
        /** Time written in the file (before any offset), or -1 for plain text. */
        double lrcSeconds = -1.0;
        /** The next time written in the file after this line (the next line,
            a blank "instrumental" line, or a trailing word time), or -1. */
        double lrcEndSeconds = -1.0;
        std::vector<Word> words;
        std::vector<Syllable> syllables;
        /** First syllable start and last syllable end, -1 while untimed. */
        double startSeconds = -1.0;
        double endSeconds = -1.0;

        bool isTimed() const { return endSeconds > startSeconds && startSeconds >= 0.0; }
        juce::String syllableText(int index) const;
    };

    std::vector<Line> lines;
    SourceTiming sourceTiming = SourceTiming::None;
    /** The file's [offset:] tag, in seconds, subtracted from every time. */
    double fileOffsetSeconds = 0.0;

    bool isEmpty() const { return lines.empty(); }
    /** True when at least one line has times (after applyTiming()). */
    bool isTimed() const;
    int numTimedLines() const;

    static KaraokeLyrics parse(const juce::String &text);

    /**
     * Character ranges of a word's syllables, covering the whole word.
     * A vowel-group heuristic; englishRules adds silent final e/ed/es.
     * Empty for a word without letters.
     */
    static std::vector<juce::Range<int>> splitSyllables(const juce::String &word,
                                                        bool englishRules);

    /**
     * Times every line and syllable. offsetSeconds is added to every time
     * written in the file. Without a reference, LRC lines are still timed
     * (syllables spread evenly) but plain text stays untimed.
     */
    void applyTiming(const KaraokeReference *reference, double offsetSeconds);

    /**
     * The offset that lines the file's line times up with the phrases the
     * reference sings, searched within +-maxShiftSeconds. With
     * onlyIfClearlyBetter, empty unless it matches clearly better than no
     * offset at all (so a file that is already right is left alone).
     * Empty for plain text.
     */
    std::optional<double> estimateOffset(const KaraokeReference &reference,
                                         bool onlyIfClearlyBetter,
                                         double maxShiftSeconds = 20.0) const;

    /**
     * Line to show as the current one at a reference time: the line being
     * sung, or once it is over, the next one (so it can be read ahead).
     * -1 when nothing is timed.
     */
    int currentLineAt(double referenceSeconds) const;
    /** Next timed line after a line, -1 when there is none. */
    int nextTimedLine(int line) const;
    /** Previous timed line before a line, -1 when there is none. */
    int previousTimedLine(int line) const;
};
