#include "ReferenceNotesJob.h"

#include "../Audio/EditorController.h"
#include "../UI/Main/SettingsManager.h"

ReferenceNotesJob::ReferenceNotesJob(const juce::File &file, ProgressCallback onProgress,
                                     FinishedCallback onFinished)
    : cancelled(std::make_shared<std::atomic<bool>>(false)),
      controller(std::make_shared<EditorController>(false))
{
    thread = std::thread([file, onProgress = std::move(onProgress),
                          onFinished = std::move(onFinished), cancelled = cancelled,
                          controller = controller]() {
        auto finish = [&onFinished, &cancelled](std::optional<KaraokeReference> reference,
                                                const juce::String &error) {
            if (cancelled->load())
                return;
            juce::MessageManager::callAsync(
                [onFinished, cancelled, reference = std::move(reference), error]() mutable {
                    if (!cancelled->load())
                        onFinished(std::move(reference), error);
                });
        };
        auto report = [&onProgress, &cancelled](double progress, const juce::String &message) {
            if (cancelled->load())
                return;
            juce::MessageManager::callAsync([onProgress, cancelled, progress, message]() {
                if (!cancelled->load())
                    onProgress(progress, message);
            });
        };

        report(0.01, "Reading " + file.getFileName());
        juce::AudioBuffer<float> audio;
        double sampleRate = 0.0;
        if (!EditorController::readAudioFileAsTake(file, audio, sampleRate))
        {
            finish(std::nullopt, "Could not read " + file.getFileName());
            return;
        }

        // Same detector and device as the editor, so the notes agree with it.
        SettingsManager settings;
        controller->setAnalysisLoadsVocoder(false);
        controller->setPitchDetectorType(settings.getPitchDetectorType());
        controller->setDeviceConfig(settings.getDevice(), settings.getGPUDeviceId());

        auto project = controller->analyzeHostAudio(
            audio, sampleRate, report, [&cancelled]() { return cancelled->load(); });
        if (cancelled->load())
            return;
        if (!project || project->getAudioData().f0.empty())
        {
            finish(std::nullopt, "Could not analyse " + file.getFileName());
            return;
        }

        auto reference = KaraokeReference::fromProject(*project, file.getFullPathName());
        if (!reference.isLoaded())
        {
            finish(std::nullopt, "No sung notes were found in " + file.getFileName());
            return;
        }
        finish(std::move(reference), {});
    });
}

ReferenceNotesJob::~ReferenceNotesJob()
{
    cancelled->store(true);
    // Makes analyzeAudio stop at its next checkpoint instead of finishing.
    controller->requestShutdown();
    if (thread.joinable())
        thread.join();
}
