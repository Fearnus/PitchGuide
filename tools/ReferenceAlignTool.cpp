// pitchguide-align: aligns a sung take to a reference vocal with
// ReferenceAlignment and writes the frame map as CSV, for checking the
// alignment on real recordings outside the plug-in.
//
//   pitchguide-align <reference> <take> [alignment.csv]

#include "../Source/Audio/Alignment/ReferenceAlignment.h"
#include "../Source/Utils/AudioResampler.h"
#include "../Source/Utils/Constants.h"
#include "../Source/Utils/MelSpectrogram.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

namespace {

// Mono mix at the analysis sample rate.
std::optional<std::vector<float>> loadMono(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return std::nullopt;

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    reader->read(&buffer, 0, numSamples, 0, true, true);

    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int channel = 0; channel < numChannels; ++channel)
        mono.addFrom(0, 0, buffer, channel, 0, numSamples, 1.0f / static_cast<float>(numChannels));

    if (static_cast<int>(reader->sampleRate) != SAMPLE_RATE)
        mono = AudioResampler::resample(mono, reader->sampleRate, SAMPLE_RATE);

    const float* samples = mono.getReadPointer(0);
    return std::vector<float>(samples, samples + mono.getNumSamples());
}

ReferenceAlignment::Features analyze(const std::vector<float>& audio) {
    MelSpectrogram mel;
    return ReferenceAlignment::computeFeatures(
        mel.compute(audio.data(), static_cast<int>(audio.size())));
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::fprintf(stderr, "usage: %s <reference> <take> [alignment.csv]\n", argv[0]);
        return 2;
    }

    const juce::File referenceFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const juce::File takeFile = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    const auto referenceAudio = loadMono(referenceFile);
    const auto takeAudio = loadMono(takeFile);
    if (!referenceAudio || !takeAudio) {
        std::fprintf(stderr, "could not read %s\n",
                     (!referenceAudio ? referenceFile : takeFile).getFullPathName().toRawUTF8());
        return 1;
    }

    const auto reference = analyze(*referenceAudio);
    const auto take = analyze(*takeAudio);

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

    if (argc == 4) {
        std::ofstream csv(argv[3]);
        csv << "take_frame,take_seconds,reference_frame,reference_seconds,cost,take_activity\n";
        for (int frame = 0; frame < take.numFrames; ++frame) {
            const float mapped = result.referenceFrame[static_cast<size_t>(frame)];
            csv << frame << ',' << framesToSeconds(frame) << ',' << mapped << ','
                << framesToSeconds(mapped) << ',' << result.cost[static_cast<size_t>(frame)] << ','
                << take.activity[static_cast<size_t>(frame)] << '\n';
        }
        if (!csv) {
            std::fprintf(stderr, "could not write %s\n", argv[3]);
            return 1;
        }
    }
    return 0;
}
