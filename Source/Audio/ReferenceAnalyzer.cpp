#include "ReferenceAnalyzer.h"

#include "../Utils/AudioResampler.h"
#include "../Utils/Constants.h"
#include "../Utils/MelSpectrogram.h"
#include "../Utils/PlatformPaths.h"
#include "FCPEPitchDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ReferenceAnalyzer
{
std::vector<float> loadMono(const juce::File &file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 ||
        reader->lengthInSamples > std::numeric_limits<int>::max())
        return {};

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    if (!reader->read(&buffer, 0, numSamples, 0, true, true))
        return {};

    juce::AudioBuffer<float> mono(1, numSamples);
    mono.clear();
    for (int channel = 0; channel < numChannels; ++channel)
        mono.addFrom(0, 0, buffer, channel, 0, numSamples,
                     1.0f / static_cast<float>(numChannels));

    if (static_cast<int>(reader->sampleRate) != SAMPLE_RATE)
        mono = AudioResampler::resample(mono, reader->sampleRate, SAMPLE_RATE);

    const float *samples = mono.getReadPointer(0);
    return std::vector<float>(samples, samples + mono.getNumSamples());
}

std::vector<float> detectPitch(const std::vector<float> &audio, int numFrames,
                               juce::String &error)
{
    FCPEPitchDetector fcpe;
    if (!fcpe.loadModel(PlatformPaths::getModelFile("fcpe.onnx"),
                        PlatformPaths::getModelFile("mel_filterbank.bin"),
                        PlatformPaths::getModelFile("cent_table.bin")))
    {
        error = "Could not load the FCPE pitch model: " + fcpe.getLastError();
        return {};
    }

    const auto detected =
        fcpe.extractF0(audio.data(), static_cast<int>(audio.size()), SAMPLE_RATE);
    if (detected.empty())
    {
        error = "Pitch detection failed: " + fcpe.getLastError();
        return {};
    }

    // FCPE reports every 10 ms; take the nearest report for each frame.
    constexpr double fcpeFrameSeconds =
        static_cast<double>(FCPEPitchDetector::HOP_SIZE) /
        FCPEPitchDetector::FCPE_SAMPLE_RATE;
    std::vector<float> f0(static_cast<size_t>(std::max(0, numFrames)), 0.0f);
    for (int frame = 0; frame < numFrames; ++frame)
    {
        const auto index = static_cast<size_t>(
            std::lround(framesToSeconds(frame) / fcpeFrameSeconds));
        f0[static_cast<size_t>(frame)] = detected[std::min(index, detected.size() - 1)];
    }
    return f0;
}

std::optional<ReferenceTrack> analyzeFile(const juce::File &file, juce::String &error)
{
    const auto audio = loadMono(file);
    if (audio.empty())
    {
        error = "Could not read " + file.getFileName();
        return std::nullopt;
    }

    MelSpectrogram mel;
    ReferenceTrack reference;
    reference.sourcePath = file.getFullPathName();
    reference.features = ReferenceAlignment::computeFeatures(
        mel.compute(audio.data(), static_cast<int>(audio.size())));
    reference.f0 = detectPitch(audio, reference.features.numFrames, error);
    if (reference.f0.empty())
        return std::nullopt;
    if (std::none_of(reference.f0.begin(), reference.f0.end(),
                     [](float value) { return value > 0.0f; }))
    {
        error = "No singing was detected in " + file.getFileName();
        return std::nullopt;
    }
    return reference;
}
} // namespace ReferenceAnalyzer

ReferenceAnalysisJob::ReferenceAnalysisJob(const juce::File &file, Callback onFinished)
    : cancelled(std::make_shared<std::atomic<bool>>(false))
{
    thread = std::thread([file, onFinished = std::move(onFinished),
                          cancelled = cancelled]() mutable {
        juce::String error;
        auto reference = ReferenceAnalyzer::analyzeFile(file, error);
        if (cancelled->load())
            return;
        juce::MessageManager::callAsync(
            [onFinished = std::move(onFinished), cancelled,
             reference = std::move(reference), error]() mutable {
                if (!cancelled->load())
                    onFinished(std::move(reference), error);
            });
    });
}

ReferenceAnalysisJob::~ReferenceAnalysisJob()
{
    cancelled->store(true);
    if (thread.joinable())
        thread.join();
}
