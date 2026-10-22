#include "ReferenceTrack.h"

#include "../Utils/Constants.h"
#include "Project.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace
{
float referencePitchAt(const std::vector<float> &f0, float frame)
{
    const long index = std::lround(frame);
    if (index < 0 || static_cast<size_t>(index) >= f0.size())
        return 0.0f;
    return f0[static_cast<size_t>(index)];
}
} // namespace

void ReferenceTrack::alignTo(const AudioData &take)
{
    referenceFrameForTakeFrame.clear();
    octaveShift = 0;
    matchCost = 0.0f;
    if (!isLoaded() || take.melSpectrogram.empty())
        return;

    const auto result = ReferenceAlignment::align(
        ReferenceAlignment::computeFeatures(take.melSpectrogram), features);
    if (!result.valid)
        return;
    referenceFrameForTakeFrame = result.referenceFrame;
    matchCost = result.meanActiveCost;

    // The octave is the median pitch difference over frames sung in both,
    // rounded to whole octaves: wrong notes move individual frames, not the
    // median, and a transposition by less than an octave is a choice of key
    // that is left to the user.
    const auto &takeF0 = take.rawF0.empty() ? take.f0 : take.rawF0;
    std::vector<float> differences;
    const size_t frames = std::min(takeF0.size(), referenceFrameForTakeFrame.size());
    for (size_t frame = 0; frame < frames; ++frame)
    {
        const float reference = referencePitchAt(f0, referenceFrameForTakeFrame[frame]);
        if (takeF0[frame] > 0.0f && reference > 0.0f)
            differences.push_back(12.0f * std::log2(takeF0[frame] / reference));
    }
    if (differences.empty())
        return;
    const auto middle = differences.begin() +
                        static_cast<std::ptrdiff_t>(differences.size() / 2);
    std::nth_element(differences.begin(), middle, differences.end());
    octaveShift = 12 * static_cast<int>(std::lround(*middle / 12.0f));
}

float ReferenceTrack::targetMidiAt(int takeFrame) const
{
    if (takeFrame < 0 ||
        static_cast<size_t>(takeFrame) >= referenceFrameForTakeFrame.size())
        return 0.0f;
    const float frequency = referencePitchAt(
        f0, referenceFrameForTakeFrame[static_cast<size_t>(takeFrame)]);
    return frequency > 0.0f ? freqToMidi(frequency) + static_cast<float>(octaveShift)
                            : 0.0f;
}
