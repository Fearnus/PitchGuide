#pragma once

#include <vector>

/**
 * Time alignment of a sung take against a reference vocal (DTW).
 *
 * Both inputs are on the analysis frame grid (HOP_SIZE at SAMPLE_RATE). The
 * result maps every take frame to the reference frame sung at the same point
 * of the song, so reference pitch can be read at the take's timing.
 *
 * Frames are compared by spectral envelope (what is being sung), not by pitch:
 * the take is expected to be off-pitch or in another octave, which is the
 * reason it is being aligned in the first place.
 */
namespace ReferenceAlignment {

/** Envelope cepstra kept per frame: c1..c12. c0 (loudness) is excluded, and
 * higher coefficients would start to follow the harmonic ripple of the 128-band
 * mel spectrum, i.e. the pitch. */
constexpr int kNumCepstra = 12;

struct Features {
    int numFrames = 0;
    /** [numFrames * kNumCepstra], normalized over the active frames. */
    std::vector<float> cepstra;
    /** [numFrames] 0 = silence .. 1 = singing, relative to the loud frames. */
    std::vector<float> activity;
};

/**
 * Derives alignment features from a log-magnitude mel spectrogram [T][mels],
 * as produced by MelSpectrogram::compute().
 */
Features computeFeatures(const std::vector<std::vector<float>>& logMel);

struct Options {
    /** Frames averaged per step of the coarse search (8 = ~93 ms). */
    int coarseHop = 8;
    /** Full-rate search radius around the coarse path (64 = ~0.74 s). */
    int bandRadius = 64;
    /** Extra cost of a non-diagonal step, so tempo changes are not free. */
    float stepPenalty = 0.1f;
};

struct Result {
    bool valid = false;
    /** [take frames] reference frame sung at each take frame (fractional). */
    std::vector<float> referenceFrame;
    /** [take frames] local match cost, 0 (identical) .. 2. */
    std::vector<float> cost;
    /** Reference frames covered by the take, [referenceStart, referenceEnd). */
    int referenceStart = 0;
    int referenceEnd = 0;
    /** Mean cost over the take's active frames; lower is a better match. */
    float meanActiveCost = 0.0f;
};

/**
 * Aligns the whole take to the best-matching stretch of the reference. The
 * take may cover only part of the reference (e.g. one verse) and may start at
 * any point in it. Returns an invalid result if either input is empty.
 */
Result align(const Features& take, const Features& reference,
             const Options& options = {});

} // namespace ReferenceAlignment
