#pragma once

#include "KaraokeReference.h"
#include "LivePitchDetector.h"

#include <cmath>
#include <limits>
#include <vector>

/**
 * What the singer sang, on the host timeline, in fixed 5 ms slots.
 *
 * A slot holds the sung pitch (MIDI), 0 for "sang nothing" and kNotRecorded
 * for "the transport never passed here". Slots are addressed by time, so singing a
 * passage again simply overwrites the previous attempt - the usual way to
 * practise a part in a DAW is to loop it.
 *
 * Message thread only.
 */
class SungPitchTrail
{
public:
    static constexpr double kSlotSeconds = 0.005;
    /** Longest timeline the trail covers (2 hours). */
    static constexpr double kMaxSeconds = 2.0 * 60.0 * 60.0;

    void clear();
    bool isEmpty() const { return written == 0; }

    /** Records a frame at frame.hostSeconds - timeOffsetSeconds. Frames of a
        stopped transport are ignored. */
    void add(const LivePitchFrame &frame, double timeOffsetSeconds = 0.0);

    /** Slot value for "the transport never passed here". Not NaN: the core
        library builds with -ffast-math on GCC/Clang, which assumes there are
        no NaNs and compiles NaN checks away. */
    static constexpr float kNotRecorded = -1.0f;

    /** Pitch sung at a slot: kNotRecorded, 0 = unvoiced, else MIDI. */
    float slotValue(int slot) const
    {
        return slot >= 0 && static_cast<size_t>(slot) < slots.size()
                   ? slots[static_cast<size_t>(slot)]
                   : kNotRecorded;
    }
    static bool isRecorded(float slotValue) { return slotValue >= 0.0f; }
    static int slotAt(double seconds)
    {
        return static_cast<int>(std::floor(seconds / kSlotSeconds));
    }
    static double slotTime(int slot) { return (slot + 0.5) * kSlotSeconds; }
    int numSlots() const { return static_cast<int>(slots.size()); }

    /** Incremented on every change, so views and scores can cache. */
    unsigned int getRevision() const { return revision; }

    struct Score
    {
        /** Seconds of reference notes the transport passed while recording. */
        double noteSeconds = 0.0;
        /** Of those, seconds the singer had a voiced pitch. */
        double voicedSeconds = 0.0;
        /** Of those, seconds within the tolerance of the (transposed) note. */
        double inTuneSeconds = 0.0;

        /** In-tune share of the time the singer sang during notes, 0..1. */
        float accuracy() const
        {
            return voicedSeconds > 0.0 ? static_cast<float>(inTuneSeconds / voicedSeconds)
                                       : 0.0f;
        }
    };

    /**
     * Scores the trail against a reference placed at host time
     * referenceStartSeconds and transposed by transposeSemitones. A voiced
     * slot is in tune when it is within toleranceCents of the reference
     * (KaraokeReference::pitchErrorAt).
     */
    Score score(const KaraokeReference &reference, double referenceStartSeconds,
                float transposeSemitones, float toleranceCents) const;

private:
    std::vector<float> slots;
    int lastSlot = -1;
    int written = 0;
    unsigned int revision = 0;
};
