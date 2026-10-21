#include "ReferenceAlignment.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ReferenceAlignment {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kInfinity = std::numeric_limits<float>::infinity();

// A frame counts as singing when its mean log-mel level is within this range
// of the loud frames (95th percentile). Natural-log magnitude units, so 5 is
// about 43 dB; the softness widens the transition to avoid hard flips.
constexpr float kActivityRange = 5.0f;
constexpr float kActivitySoftness = 0.75f;
constexpr float kLoudPercentile = 0.95f;

// Cost of matching a sung frame against a silent one. Cosine distances
// between sung frames are 0..2 and mostly below 1.
constexpr float kActivityMismatchCost = 1.0f;

enum Step : std::uint8_t { kStart, kDiagonal, kTakeOnly, kReferenceOnly };

float percentile(std::vector<float> values, float fraction) {
    const auto index = static_cast<size_t>(
        fraction * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(),
                     values.begin() + static_cast<std::ptrdiff_t>(index),
                     values.end());
    return values[index];
}

const float* frameCepstra(const Features& features, int frame) {
    return features.cepstra.data() + static_cast<size_t>(frame) * kNumCepstra;
}

// Cosine distance between envelopes while both frames are sung, plus a cost
// for how much more one frame is sung than the other. Two silences cost
// nothing, and neither do two identical frames during a fade.
float frameCost(const Features& take, int takeFrame, const Features& reference,
                int referenceFrame) {
    const float* a = frameCepstra(take, takeFrame);
    const float* b = frameCepstra(reference, referenceFrame);
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (int k = 0; k < kNumCepstra; ++k) {
        dot += a[k] * b[k];
        normA += a[k] * a[k];
        normB += b[k] * b[k];
    }
    const float denominator = std::sqrt(normA * normB);
    const float cosineDistance =
        denominator > 1e-12f ? 1.0f - dot / denominator : 1.0f;

    const float activityA = take.activity[static_cast<size_t>(takeFrame)];
    const float activityB = reference.activity[static_cast<size_t>(referenceFrame)];
    return activityA * activityB * cosineDistance +
           std::abs(activityA - activityB) * kActivityMismatchCost;
}

// Averages blocks of `hop` frames. Cepstra are weighted by activity so the
// silent part of a block does not blur the sung part.
Features decimate(const Features& in, int hop) {
    Features out;
    out.numFrames = (in.numFrames + hop - 1) / hop;
    out.cepstra.assign(static_cast<size_t>(out.numFrames) * kNumCepstra, 0.0f);
    out.activity.assign(static_cast<size_t>(out.numFrames), 0.0f);

    for (int block = 0; block < out.numFrames; ++block) {
        const int begin = block * hop;
        const int end = std::min(in.numFrames, begin + hop);
        float* destination = out.cepstra.data() + static_cast<size_t>(block) * kNumCepstra;
        float weight = 0.0f;
        for (int frame = begin; frame < end; ++frame) {
            const float activity = in.activity[static_cast<size_t>(frame)];
            const float* source = frameCepstra(in, frame);
            for (int k = 0; k < kNumCepstra; ++k)
                destination[k] += activity * source[k];
            weight += activity;
        }
        if (weight > 0.0f)
            for (int k = 0; k < kNumCepstra; ++k)
                destination[k] /= weight;
        out.activity[static_cast<size_t>(block)] =
            weight / static_cast<float>(end - begin);
    }
    return out;
}

struct Path {
    std::vector<float> columnForRow; // mean reference frame per take frame
    int firstColumn = 0;
    int lastColumn = 0;
};

// DTW of take rows against reference columns, restricted to columns
// [low[row], high[row]] (both non-decreasing). The path may begin in any column
// of the first row and end in any column of the last row, which lets a take
// match a stretch inside a longer reference.
Path bandedDtw(const Features& take, const Features& reference,
               const std::vector<int>& low, const std::vector<int>& high,
               float stepPenalty) {
    const int rows = take.numFrames;

    std::vector<size_t> rowOffset(static_cast<size_t>(rows) + 1, 0);
    for (int row = 0; row < rows; ++row)
        rowOffset[static_cast<size_t>(row) + 1] =
            rowOffset[static_cast<size_t>(row)] +
            static_cast<size_t>(high[static_cast<size_t>(row)] - low[static_cast<size_t>(row)] + 1);
    std::vector<std::uint8_t> steps(rowOffset.back(), kStart);

    std::vector<float> previous, current;
    for (int row = 0; row < rows; ++row) {
        const int rowLow = low[static_cast<size_t>(row)];
        const int rowHigh = high[static_cast<size_t>(row)];
        current.assign(static_cast<size_t>(rowHigh - rowLow + 1), kInfinity);
        std::uint8_t* rowSteps = steps.data() + rowOffset[static_cast<size_t>(row)];
        const int previousLow = row > 0 ? low[static_cast<size_t>(row) - 1] : 0;
        const int previousHigh = row > 0 ? high[static_cast<size_t>(row) - 1] : -1;

        for (int column = rowLow; column <= rowHigh; ++column) {
            const size_t cell = static_cast<size_t>(column - rowLow);
            float best = row == 0 ? 0.0f : kInfinity;
            std::uint8_t step = kStart;

            if (row > 0) {
                if (column - 1 >= previousLow && column - 1 <= previousHigh) {
                    best = previous[static_cast<size_t>(column - 1 - previousLow)];
                    step = kDiagonal;
                }
                if (column >= previousLow && column <= previousHigh) {
                    const float candidate =
                        previous[static_cast<size_t>(column - previousLow)] + stepPenalty;
                    if (candidate < best) {
                        best = candidate;
                        step = kTakeOnly;
                    }
                }
            }
            if (column > rowLow) {
                const float candidate = current[cell - 1] + stepPenalty;
                if (candidate < best) {
                    best = candidate;
                    step = kReferenceOnly;
                }
            }

            if (best < kInfinity) {
                current[cell] = best + frameCost(take, row, reference, column);
                rowSteps[cell] = step;
            }
        }
        std::swap(previous, current);
    }

    // Cheapest end in the last row, then walk the steps back to the start.
    const int lastLow = low[static_cast<size_t>(rows) - 1];
    const auto end = std::min_element(previous.begin(), previous.end());
    int column = lastLow + static_cast<int>(end - previous.begin());

    Path path;
    path.lastColumn = column;
    path.columnForRow.assign(static_cast<size_t>(rows), 0.0f);
    std::vector<int> columnsInRow(static_cast<size_t>(rows), 0);
    int row = rows - 1;
    for (;;) {
        path.columnForRow[static_cast<size_t>(row)] += static_cast<float>(column);
        ++columnsInRow[static_cast<size_t>(row)];
        const auto step = steps[rowOffset[static_cast<size_t>(row)] +
                                static_cast<size_t>(column - low[static_cast<size_t>(row)])];
        if (step == kStart)
            break;
        if (step != kReferenceOnly)
            --row;
        if (step != kTakeOnly)
            --column;
    }
    path.firstColumn = column;
    for (size_t r = 0; r < path.columnForRow.size(); ++r)
        path.columnForRow[r] /= static_cast<float>(columnsInRow[r]);
    return path;
}

float interpolate(const std::vector<float>& values, float position) {
    const float clamped =
        std::clamp(position, 0.0f, static_cast<float>(values.size() - 1));
    const auto index = static_cast<size_t>(clamped);
    if (index + 1 >= values.size())
        return values.back();
    const float fraction = clamped - static_cast<float>(index);
    return values[index] + fraction * (values[index + 1] - values[index]);
}

} // namespace

Features computeFeatures(const std::vector<std::vector<float>>& logMel) {
    Features features;
    const int numFrames = static_cast<int>(logMel.size());
    if (numFrames == 0 || logMel.front().empty())
        return features;
    const int numMels = static_cast<int>(logMel.front().size());

    // DCT-II rows for c1..c12.
    std::vector<float> basis(static_cast<size_t>(kNumCepstra) * static_cast<size_t>(numMels));
    for (int k = 0; k < kNumCepstra; ++k)
        for (int m = 0; m < numMels; ++m)
            basis[static_cast<size_t>(k * numMels + m)] = std::cos(
                kPi * static_cast<float>(k + 1) * (static_cast<float>(m) + 0.5f) /
                static_cast<float>(numMels));

    features.numFrames = numFrames;
    features.cepstra.assign(static_cast<size_t>(numFrames) * kNumCepstra, 0.0f);
    features.activity.assign(static_cast<size_t>(numFrames), 0.0f);
    std::vector<float> level(static_cast<size_t>(numFrames), 0.0f);

    for (int frame = 0; frame < numFrames; ++frame) {
        const auto& bands = logMel[static_cast<size_t>(frame)];
        const int usable = std::min(numMels, static_cast<int>(bands.size()));
        float sum = 0.0f;
        for (int m = 0; m < usable; ++m)
            sum += bands[static_cast<size_t>(m)];
        level[static_cast<size_t>(frame)] = usable > 0 ? sum / static_cast<float>(usable) : 0.0f;

        float* cepstra = features.cepstra.data() + static_cast<size_t>(frame) * kNumCepstra;
        for (int k = 0; k < kNumCepstra; ++k) {
            const float* row = basis.data() + static_cast<size_t>(k * numMels);
            float value = 0.0f;
            for (int m = 0; m < usable; ++m)
                value += bands[static_cast<size_t>(m)] * row[m];
            cepstra[k] = value;
        }
    }

    const float threshold = percentile(level, kLoudPercentile) - kActivityRange;
    for (int frame = 0; frame < numFrames; ++frame)
        features.activity[static_cast<size_t>(frame)] =
            1.0f / (1.0f + std::exp(-(level[static_cast<size_t>(frame)] - threshold) /
                                    kActivitySoftness));

    // Normalize each coefficient over the sung frames, so that a different
    // singer, microphone or room moves the features less than a different
    // vowel does.
    float totalWeight = 0.0f;
    for (float activity : features.activity)
        totalWeight += activity;
    const bool weighted = totalWeight >= 1.0f;
    if (!weighted)
        totalWeight = static_cast<float>(numFrames);

    for (int k = 0; k < kNumCepstra; ++k) {
        double sum = 0.0, sumSquares = 0.0;
        for (int frame = 0; frame < numFrames; ++frame) {
            const double weight = weighted ? features.activity[static_cast<size_t>(frame)] : 1.0;
            const double value = features.cepstra[static_cast<size_t>(frame) * kNumCepstra + static_cast<size_t>(k)];
            sum += weight * value;
            sumSquares += weight * value * value;
        }
        const double mean = sum / totalWeight;
        const double deviation =
            std::sqrt(std::max(sumSquares / totalWeight - mean * mean, 1e-12));
        for (int frame = 0; frame < numFrames; ++frame) {
            auto& value = features.cepstra[static_cast<size_t>(frame) * kNumCepstra + static_cast<size_t>(k)];
            value = static_cast<float>((value - mean) / deviation);
        }
    }
    return features;
}

Result align(const Features& take, const Features& reference,
             const Options& options) {
    Result result;
    if (take.numFrames == 0 || reference.numFrames == 0)
        return result;

    // Coarse pass: find where in the reference the take is sung.
    const int hop = std::max(1, options.coarseHop);
    const float stepPenalty = std::max(0.0f, options.stepPenalty);
    const auto coarseTake = decimate(take, hop);
    const auto coarseReference = decimate(reference, hop);
    const auto coarse = bandedDtw(
        coarseTake, coarseReference,
        std::vector<int>(static_cast<size_t>(coarseTake.numFrames), 0),
        std::vector<int>(static_cast<size_t>(coarseTake.numFrames),
                         coarseReference.numFrames - 1),
        stepPenalty);

    // Fine pass: full rate, within bandRadius of the coarse path. Block i
    // covers frames [i * hop, (i + 1) * hop), so its center is i * hop + halfBlock.
    const int numTake = take.numFrames;
    const int lastReference = reference.numFrames - 1;
    const int radius = std::max(1, options.bandRadius);
    const float halfBlock = 0.5f * static_cast<float>(hop - 1);
    std::vector<int> low(static_cast<size_t>(numTake)), high(static_cast<size_t>(numTake));
    for (int frame = 0; frame < numTake; ++frame) {
        const float block = (static_cast<float>(frame) - halfBlock) / static_cast<float>(hop);
        const float center =
            interpolate(coarse.columnForRow, block) * static_cast<float>(hop) + halfBlock;
        const int rounded = static_cast<int>(std::lround(center));
        int rowLow = std::clamp(rounded - radius, 0, lastReference);
        int rowHigh = std::clamp(rounded + radius, 0, lastReference);
        if (frame > 0) {
            // Keep the band monotonic and connected to the previous row.
            const size_t previous = static_cast<size_t>(frame) - 1;
            rowLow = std::min(std::max(rowLow, low[previous]), high[previous]);
            rowHigh = std::max(rowHigh, high[previous]);
        }
        low[static_cast<size_t>(frame)] = rowLow;
        high[static_cast<size_t>(frame)] = rowHigh;
    }
    const auto fine = bandedDtw(take, reference, low, high, stepPenalty);

    result.referenceFrame = fine.columnForRow;
    result.referenceStart = fine.firstColumn;
    result.referenceEnd = fine.lastColumn + 1;
    result.cost.resize(static_cast<size_t>(numTake));
    float weightedCost = 0.0f, activeWeight = 0.0f;
    for (int frame = 0; frame < numTake; ++frame) {
        const int column = std::clamp(
            static_cast<int>(std::lround(result.referenceFrame[static_cast<size_t>(frame)])),
            0, lastReference);
        const float cost = frameCost(take, frame, reference, column);
        result.cost[static_cast<size_t>(frame)] = cost;
        weightedCost += take.activity[static_cast<size_t>(frame)] * cost;
        activeWeight += take.activity[static_cast<size_t>(frame)];
    }
    result.meanActiveCost = activeWeight > 0.0f ? weightedCost / activeWeight : 0.0f;
    result.valid = true;
    return result;
}

} // namespace ReferenceAlignment
