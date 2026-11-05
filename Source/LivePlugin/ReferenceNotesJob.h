#pragma once

#include "../Audio/Live/KaraokeReference.h"
#include "../JuceHeader.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

class EditorController;

/**
 * Finds the notes of a reference vocal on a background thread.
 *
 * The file goes through the editor's take analysis unchanged - read like a
 * take file (EditorController::readAudioFileAsTake), then analyzed like a
 * captured take (EditorController::analyzeHostAudio: FCPE or RMVPE pitch and
 * GAME notes, on the device chosen in PitchGuide's settings) - so PitchGuide
 * Live shows the same notes the editor would for the same file. Only the
 * vocoder, which note detection never uses, is not loaded.
 *
 * Callbacks arrive on the message thread. Destroying the job cancels it and
 * waits for the thread; no callback runs after that.
 */
class ReferenceNotesJob
{
public:
    using ProgressCallback = std::function<void(double progress, const juce::String &message)>;
    using FinishedCallback =
        std::function<void(std::optional<KaraokeReference> reference, const juce::String &error)>;

    ReferenceNotesJob(const juce::File &file, ProgressCallback onProgress,
                      FinishedCallback onFinished);
    ~ReferenceNotesJob();

    ReferenceNotesJob(const ReferenceNotesJob &) = delete;
    ReferenceNotesJob &operator=(const ReferenceNotesJob &) = delete;

private:
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::shared_ptr<EditorController> controller;
    std::thread thread;
};
