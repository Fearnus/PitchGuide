#pragma once

#include "../JuceHeader.h"
#include "../Models/ReferenceTrack.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

/**
 * Analyzes a reference vocal file into a ReferenceTrack: alignment features
 * from its mel spectrogram and pitch from FCPE. FCPE runs on the CPU: a
 * reference is analyzed once, and the GPU stays free for the editor.
 */
namespace ReferenceAnalyzer
{
/** Mono mix of an audio file at SAMPLE_RATE; empty if it cannot be read. */
std::vector<float> loadMono(const juce::File &file);

/**
 * Pitch in Hz on the analysis frame grid, 0 where unvoiced. Empty, with
 * `error` set, if FCPE cannot be loaded or run.
 */
std::vector<float> detectPitch(const std::vector<float> &audio, int numFrames,
                               juce::String &error);

/** Full analysis of `file`. Blocking; std::nullopt with `error` set on failure. */
std::optional<ReferenceTrack> analyzeFile(const juce::File &file,
                                          juce::String &error);
} // namespace ReferenceAnalyzer

/**
 * Runs ReferenceAnalyzer::analyzeFile on a background thread and reports the
 * result on the message thread. Destroying the job waits for the thread and
 * drops the result, so the analysis never outlives the editor or plug-in that
 * started it.
 */
class ReferenceAnalysisJob
{
public:
    using Callback =
        std::function<void(std::optional<ReferenceTrack> reference, juce::String error)>;

    ReferenceAnalysisJob(const juce::File &file, Callback onFinished);
    ~ReferenceAnalysisJob();

    ReferenceAnalysisJob(const ReferenceAnalysisJob &) = delete;
    ReferenceAnalysisJob &operator=(const ReferenceAnalysisJob &) = delete;

private:
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::thread thread;
};
