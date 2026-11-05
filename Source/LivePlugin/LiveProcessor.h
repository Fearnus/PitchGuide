#pragma once

#include "../Audio/Live/KaraokeReference.h"
#include "../Audio/Live/LivePitchDetector.h"
#include "../Audio/Live/SungPitchTrail.h"
#include "../JuceHeader.h"
#include "ReferenceNotesJob.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

/**
 * PitchGuide Live: karaoke pitch feedback.
 *
 * Sits on the singer's (monitored) microphone track and passes the audio
 * through untouched. A reference vocal of the song is analysed once into
 * notes; while the host plays, the microphone's pitch is detected live and
 * recorded against the host timeline, so the editor can draw what was sung
 * over what should have been sung.
 *
 * Threads: processBlock() runs the detector and pushes frames into a
 * lock-free FIFO; everything else, including the sung trail, lives on the
 * message thread.
 */
class PitchGuideLiveProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PitchGuideLiveProcessor();
    ~PitchGuideLiveProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    // ---- Message thread from here on ----------------------------------

    /** Semitones added to the reference, so a singer can move it into range. */
    int getTranspose() const;
    void setTranspose(int semitones);
    static constexpr int kMaxTranspose = 24;

    struct Options
    {
        /** Host time at which the reference file starts. */
        double referenceStartSeconds = 0.0;
        /** Input + output latency of the singer's monitoring path: sung
            frames are moved this much earlier before they are recorded. */
        float latencyMs = 0.0f;
        /** How far from the note still counts as in tune. */
        float toleranceCents = 50.0f;
        /** Input level below which nothing is detected. */
        float gateDb = -50.0f;
        float pixelsPerSecond = 160.0f;
        int editorWidth = 980;
        int editorHeight = 520;
    };
    const Options &getOptions() const { return options; }
    void setOptions(const Options &newOptions);

    const KaraokeReference &getReference() const { return reference; }
    /** Starts analysing a reference vocal; the current one stays until it is done. */
    void loadReference(const juce::File &file);
    void clearReference();

    bool isAnalysing() const { return analysisJob != nullptr; }
    double getAnalysisProgress() const { return analysisProgress; }
    const juce::String &getAnalysisMessage() const { return analysisMessage; }
    const juce::String &getLastError() const { return lastError; }

    /** Moves frames from the audio thread into the trail. Called by a timer
        and by the editor right before it paints. */
    void drainLiveFrames();
    const SungPitchTrail &getTrail() const { return trail; }
    void clearTrail() { trail.clear(); }

    /** Latest detected frame, and when (message-thread wall clock, ms) it
        was received. */
    const LivePitchFrame &getLatestFrame() const { return latestFrame; }
    double getLatestFrameWallMs() const { return latestFrameWallMs; }

    /** The most recent frames, oldest first, regardless of the transport. */
    static constexpr int kRecentFrames = 1024;
    template <typename Fn> void forEachRecentFrame(Fn &&fn) const
    {
        const int count = std::min(recentCount, kRecentFrames);
        for (int i = 0; i < count; ++i)
            fn(recent[static_cast<size_t>((recentHead - count + i + kRecentFrames) %
                                          kRecentFrames)]);
    }

    /** Host position now, extrapolated from the last audio block with the
        wall clock so the view scrolls smoothly between blocks. */
    double estimateHostSeconds() const;
    bool isHostPlaying() const { return transportPlaying.load(); }
    bool hasSeenAudio() const { return transportWallMs.load() > 0.0; }

    juce::AudioProcessorValueTreeState &getParameterState() { return parameters; }

private:
    void timerCallback() override { drainLiveFrames(); }
    void applyReference(KaraokeReference newReference);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> *transposeParameter = nullptr;

    // Audio thread.
    LivePitchDetector detector;
    juce::AudioBuffer<float> monoScratch;
    std::atomic<float> gateDb{-50.0f};

    // Audio thread -> message thread.
    static constexpr int kFifoSize = 4096;
    juce::AbstractFifo fifo{kFifoSize};
    std::array<LivePitchFrame, kFifoSize> fifoFrames{};
    std::atomic<double> transportSeconds{0.0};
    std::atomic<double> transportWallMs{0.0};
    std::atomic<double> transportSampleRate{44100.0};
    std::atomic<bool> transportPlaying{false};

    // What getStateInformation() saves, kept ready under a lock because some
    // hosts ask for the state from a thread other than the message thread.
    juce::CriticalSection stateLock;
    Options savedOptions;
    juce::String savedReferencePath;
    juce::String savedReferenceData;
    void updateSavedState();

    // Message thread.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    Options options;
    juce::File analysingFile;
    KaraokeReference reference;
    std::unique_ptr<ReferenceNotesJob> analysisJob;
    double analysisProgress = 0.0;
    juce::String analysisMessage;
    juce::String lastError;
    SungPitchTrail trail;
    LivePitchFrame latestFrame;
    double latestFrameWallMs = 0.0;
    std::array<LivePitchFrame, kRecentFrames> recent{};
    int recentHead = 0;
    int recentCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchGuideLiveProcessor)
};
