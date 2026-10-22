#pragma once

#include "../Audio/Alignment/ReferenceAlignment.h"
#include "../JuceHeader.h"
#include <vector>

struct AudioData;

/**
 * A reference vocal the take is guided towards.
 *
 * Its pitch and alignment features are analyzed once, when the reference is
 * loaded. The alignment to the project's take is derived from them and
 * recomputed whenever the take changes. All frames are on the analysis grid
 * (HOP_SIZE at SAMPLE_RATE), and the reference starts at song time 0, like
 * the project's timeline.
 */
struct ReferenceTrack
{
    /** File the reference was analyzed from (shown to the user). */
    juce::String sourcePath;
    /** [reference frames] detected pitch in Hz, 0 where unvoiced. */
    std::vector<float> f0;
    ReferenceAlignment::Features features;

    /** [take frames] reference frame sung at each take frame; empty until
     * aligned. */
    std::vector<float> referenceFrameForTakeFrame;
    /** Whole octaves (in semitones) between the reference and the take, so
     * that a take sung an octave lower is guided an octave lower. */
    int octaveShift = 0;
    /** How well the take matched the reference, 0 = identical; see
     * ReferenceAlignment::Result::meanActiveCost. */
    float matchCost = 0.0f;

    bool isLoaded() const { return !f0.empty(); }
    bool isAligned() const { return !referenceFrameForTakeFrame.empty(); }

    /**
     * Aligns the reference to the take in `take`, using its mel spectrogram
     * for timing and its detected pitch for the octave. Clears the alignment
     * if either side has nothing to align.
     */
    void alignTo(const AudioData &take);

    /**
     * Reference pitch sung at a take frame, as a MIDI note with the octave
     * shift applied; 0 where the reference is silent or nothing is aligned.
     */
    float targetMidiAt(int takeFrame) const;
};
