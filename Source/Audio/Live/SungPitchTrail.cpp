#include "SungPitchTrail.h"

#include <algorithm>

namespace
{
// Frames arrive every ~5 ms, so consecutive frames of one pass can land one
// slot apart or skip a slot. Gaps up to this many slots are filled; a longer
// jump is a seek, and the slots in between are left as they were.
constexpr int kMaxFilledGapSlots = 4;
} // namespace

void SungPitchTrail::clear()
{
    slots.clear();
    lastSlot = -1;
    written = 0;
    ++revision;
}

void SungPitchTrail::add(const LivePitchFrame &frame, double timeOffsetSeconds)
{
    if (!frame.hostPlaying)
    {
        lastSlot = -1;
        return;
    }

    const double seconds = frame.hostSeconds - timeOffsetSeconds;
    if (!(seconds >= 0.0) || seconds >= kMaxSeconds)
        return;

    const int slot = slotAt(seconds);
    if (static_cast<size_t>(slot) >= slots.size())
    {
        // Grow in whole minutes so a song does not reallocate per frame.
        constexpr int kGrowSlots = static_cast<int>(60.0 / kSlotSeconds);
        const size_t newSize =
            (static_cast<size_t>(slot) / kGrowSlots + 1) * static_cast<size_t>(kGrowSlots);
        slots.resize(newSize, kNotRecorded);
    }

    const float value = frame.isVoiced() ? frame.midi : 0.0f;
    const int gap = slot - lastSlot;
    if (lastSlot >= 0 && gap > 1 && gap <= kMaxFilledGapSlots + 1)
    {
        const float previous = slots[static_cast<size_t>(lastSlot)];
        const bool bothVoiced = previous > 0.0f && value > 0.0f;
        for (int fill = lastSlot + 1; fill < slot; ++fill)
        {
            const float t = static_cast<float>(fill - lastSlot) / static_cast<float>(gap);
            slots[static_cast<size_t>(fill)] =
                bothVoiced ? previous + (value - previous) * t : value;
        }
    }

    slots[static_cast<size_t>(slot)] = value;
    lastSlot = slot;
    ++written;
    ++revision;
}

SungPitchTrail::Score SungPitchTrail::score(const KaraokeReference &reference,
                                            double referenceStartSeconds,
                                            float transposeSemitones,
                                            float toleranceCents) const
{
    Score result;
    const float tolerance = std::max(0.0f, toleranceCents) / 100.0f;
    for (const auto &note : reference.notes)
    {
        const double start = note.startSeconds + referenceStartSeconds;
        const double end = note.endSeconds + referenceStartSeconds;
        if (end <= 0.0)
            continue;
        const int first = std::max(0, slotAt(start));
        const int last = std::min(numSlots(), slotAt(end) + 1);
        for (int slot = first; slot < last; ++slot)
        {
            const double centre = slotTime(slot);
            if (centre < start || centre >= end)
                continue;
            const float sung = slots[static_cast<size_t>(slot)];
            if (!isRecorded(sung))
                continue;
            result.noteSeconds += kSlotSeconds;
            if (sung <= 0.0f)
                continue;
            result.voicedSeconds += kSlotSeconds;
            const auto error = reference.pitchErrorAt(centre - referenceStartSeconds, sung,
                                                      transposeSemitones);
            if (error && std::abs(*error) <= tolerance)
                result.inTuneSeconds += kSlotSeconds;
        }
    }
    return result;
}
