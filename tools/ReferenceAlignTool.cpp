// pitchguide-align: aligns a sung take to a reference vocal with
// ReferenceAlignment and writes the frame map as CSV, for checking the
// alignment on real recordings outside the plug-in.
//
//   pitchguide-align <reference> <take> [alignment.csv]
//
// When FCPE's model files are found (run from the repository root), it also
// detects pitch in both files and reports how well the take's pitch follows
// the aligned reference, compared with reading the reference at the same
// timestamp. Real takes have no ground truth, so this is the accuracy check.

#include "../Source/Audio/Alignment/ReferenceAlignment.h"
#include "../Source/Audio/ReferenceAnalyzer.h"
#include "../Source/Utils/Constants.h"
#include "../Source/Utils/MelSpectrogram.h"
#include "../Source/Utils/OnnxRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

namespace {

ReferenceAlignment::Features analyze(const std::vector<float>& audio) {
    MelSpectrogram mel;
    return ReferenceAlignment::computeFeatures(
        mel.compute(audio.data(), static_cast<int>(audio.size())));
}

float referencePitchAt(const std::vector<float>& referenceF0, float frame) {
    const auto index = static_cast<size_t>(std::max(0L, std::lround(frame)));
    return index < referenceF0.size() ? referenceF0[index] : 0.0f;
}

struct PitchAgreement {
    int frames = 0;          // voiced in both
    float medianSemitones = 0;
    float withinOne = 0;     // fraction within 1 semitone after the octave shift
    float withinHalf = 0;
};

// Compares take pitch with reference pitch read at `referenceFrameFor(frame)`,
// after removing the whole-octave offset that best fits.
template <typename FrameMap>
PitchAgreement comparePitch(const std::vector<float>& takeF0,
                            const std::vector<float>& referenceF0, FrameMap referenceFrameFor,
                            int octaveShift) {
    std::vector<float> differences;
    for (size_t frame = 0; frame < takeF0.size(); ++frame) {
        const float reference = referencePitchAt(referenceF0, referenceFrameFor(frame));
        if (takeF0[frame] > 0.0f && reference > 0.0f)
            differences.push_back(12.0f * std::log2(takeF0[frame] / reference));
    }
    PitchAgreement agreement;
    agreement.frames = static_cast<int>(differences.size());
    if (differences.empty())
        return agreement;
    int one = 0, half = 0;
    for (float difference : differences) {
        const float error = std::abs(difference - static_cast<float>(octaveShift));
        one += error <= 1.0f;
        half += error <= 0.5f;
    }
    std::nth_element(differences.begin(), differences.begin() + static_cast<std::ptrdiff_t>(differences.size() / 2),
                     differences.end());
    agreement.medianSemitones = differences[differences.size() / 2];
    agreement.withinOne = static_cast<float>(one) / static_cast<float>(differences.size());
    agreement.withinHalf = static_cast<float>(half) / static_cast<float>(differences.size());
    return agreement;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: %s <reference> <take> [alignment.csv]\n", argv[0]);
        return 2;
    }

    const juce::File referenceFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const juce::File takeFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    const auto referenceAudio = ReferenceAnalyzer::loadMono(referenceFile);
    const auto takeAudio = ReferenceAnalyzer::loadMono(takeFile);
    if (referenceAudio.empty() || takeAudio.empty()) {
        std::fprintf(stderr, "could not read %s\n",
                     (referenceAudio.empty() ? referenceFile : takeFile).getFullPathName().toRawUTF8());
        return 1;
    }

    const auto reference = analyze(referenceAudio);
    const auto take = analyze(takeAudio);

    const auto started = std::chrono::steady_clock::now();
    const auto result = ReferenceAlignment::align(take, reference);
    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started).count();
    if (!result.valid) {
        std::fprintf(stderr, "alignment failed\n");
        return 1;
    }

    std::printf("reference      %.2f s (%d frames)\n", framesToSeconds(reference.numFrames), reference.numFrames);
    std::printf("take           %.2f s (%d frames)\n", framesToSeconds(take.numFrames), take.numFrames);
    std::printf("matched        reference %.2f s .. %.2f s\n",
                framesToSeconds(result.referenceStart), framesToSeconds(result.referenceEnd));
    std::printf("mean cost      %.3f (sung frames; 0 = identical)\n", result.meanActiveCost);
    std::printf("alignment took %.1f ms\n", elapsed);

    std::vector<float> takeF0, referenceF0;
    juce::String pitchError;
    if (OnnxRuntime::initialise(&pitchError))
        takeF0 = ReferenceAnalyzer::detectPitch(takeAudio, take.numFrames, pitchError);
    if (!takeF0.empty())
        referenceF0 = ReferenceAnalyzer::detectPitch(referenceAudio, reference.numFrames, pitchError);
    if (!referenceF0.empty()) {

        const auto aligned = [&](size_t frame) { return result.referenceFrame[frame]; };
        const auto sameTime = [](size_t frame) { return static_cast<float>(frame); };
        const auto first = comparePitch(takeF0, referenceF0, aligned, 0);
        const int octaveShift = 12 * static_cast<int>(std::lround(first.medianSemitones / 12.0f));
        const auto withAlignment = comparePitch(takeF0, referenceF0, aligned, octaveShift);
        const auto withoutAlignment = comparePitch(takeF0, referenceF0, sameTime, octaveShift);
        std::printf("octave shift   %+d semitones (median take - reference: %+.2f)\n",
                    octaveShift, withAlignment.medianSemitones);
        std::printf("pitch match    aligned:   %5.1f%% within 1 semitone, %5.1f%% within 0.5 (%d frames)\n",
                    100.0f * withAlignment.withinOne, 100.0f * withAlignment.withinHalf,
                    withAlignment.frames);
        std::printf("               same time: %5.1f%% within 1 semitone, %5.1f%% within 0.5 (%d frames)\n",
                    100.0f * withoutAlignment.withinOne, 100.0f * withoutAlignment.withinHalf,
                    withoutAlignment.frames);
    } else {
        takeF0.clear();
        std::printf("pitch          skipped (%s; run from the repository root)\n",
                    pitchError.toRawUTF8());
    }

    if (argc == 4) {
        std::ofstream csv(argv[3]);
        csv << "take_frame,take_seconds,reference_frame,reference_seconds,cost,take_activity";
        if (!takeF0.empty())
            csv << ",take_f0_hz,reference_f0_hz";
        csv << '\n';
        for (int frame = 0; frame < take.numFrames; ++frame) {
            const auto index = static_cast<size_t>(frame);
            const float mapped = result.referenceFrame[index];
            csv << frame << ',' << framesToSeconds(frame) << ',' << mapped << ','
                << framesToSeconds(mapped) << ',' << result.cost[index] << ','
                << take.activity[index];
            if (!takeF0.empty())
                csv << ',' << takeF0[index] << ',' << referencePitchAt(referenceF0, mapped);
            csv << '\n';
        }
        if (!csv) {
            std::fprintf(stderr, "could not write %s\n", argv[3]);
            return 1;
        }
    }
    return 0;
}
