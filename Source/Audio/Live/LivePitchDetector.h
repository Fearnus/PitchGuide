#pragma once

#include <array>
#include <cstdint>
#include <vector>

/**
 * One analysis frame of a live input, as LivePitchDetector reports it.
 *
 * Times describe the centre of the analysed window, which is the moment the
 * detected pitch was actually sung, not the moment the detector finished.
 */
struct LivePitchFrame
{
    /** Input-stream time in seconds: counts every sample the detector was fed,
        whether or not the host transport was running. Always increasing. */
    double streamSeconds = 0.0;
    /** Host timeline position in seconds. Meaningful while hostPlaying. */
    double hostSeconds = 0.0;
    /** Detected pitch as a (fractional) MIDI note, 0 when unvoiced. */
    float midi = 0.0f;
    /** Periodicity of the window, 0..1 (1 - the YIN aperiodicity). */
    float confidence = 0.0f;
    /** RMS level of the window in dBFS. */
    float levelDb = -120.0f;
    bool hostPlaying = false;

    bool isVoiced() const { return midi > 0.0f; }
};

/**
 * Low-latency monophonic pitch detector for a live voice (YIN).
 *
 * The input is low-passed and decimated to roughly 16 kHz, then a window just
 * long enough for the lowest pitch (one period of integration plus one period
 * of lag, ~31 ms at 65 Hz) is analysed every 5 ms. A frame therefore reaches
 * the UI about half a window plus one hop after it was sung.
 *
 * process() is real-time safe: it does not allocate, lock or call the system.
 * prepare() and reset() allocate and must not run on the audio thread.
 */
class LivePitchDetector
{
public:
    struct Settings
    {
        float minHz = 65.0f;            // ~C2, the bottom of a bass voice
        float maxHz = 1100.0f;          // ~C#6, the top of a soprano
        float yinThreshold = 0.15f;     // first dip of the CMNDF below this wins
        float maxAperiodicity = 0.40f;  // a best dip above this is unvoiced
        float gateDb = -50.0f;          // quieter windows are unvoiced
        double hopSeconds = 0.005;
        bool medianSmoothing = true;    // median of 3 frames: removes one-frame
                                        // octave blips for one hop of delay
    };

    LivePitchDetector() = default;

    void prepare(double hostSampleRate, const Settings &settings);
    void prepare(double hostSampleRate) { prepare(hostSampleRate, Settings{}); }
    /** Forgets the input history; the next frames need a full window again. */
    void reset();

    /** Real-time safe. */
    void setGateDb(float db) { settings.gateDb = db; }
    const Settings &getSettings() const { return settings; }

    /**
     * Feeds a block of mono input. blockHostSeconds is the host position of
     * samples[0] and hostPlaying whether the host transport is advancing.
     * Calls onFrame(const LivePitchFrame &) on this thread for every frame
     * completed by the block.
     */
    template <typename FrameCallback>
    void process(const float *samples, int numSamples, double blockHostSeconds,
                 bool hostPlaying, FrameCallback &&onFrame)
    {
        if (!prepared)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            if (!pushSample(samples[i]))
                continue;
            LivePitchFrame frame;
            if (!analyse(frame))
                continue;
            // The window ends at input sample i; its centre is half a window
            // back. With smoothing on, the reported frame is the previous one.
            const double centreOffsetSamples =
                static_cast<double>(i + 1) - windowCentreLagInputSamples();
            frame.hostSeconds = blockHostSeconds + centreOffsetSamples / hostRate;
            frame.streamSeconds =
                (static_cast<double>(streamSamples) + centreOffsetSamples) / hostRate;
            frame.hostPlaying = hostPlaying;
            onFrame(static_cast<const LivePitchFrame &>(frame));
        }
        streamSamples += numSamples;
    }

    double getHostSampleRate() const { return hostRate; }
    double getAnalysisSampleRate() const { return analysisRate; }
    int getDecimation() const { return decimation; }
    int getWindowSize() const { return windowSize; }
    int getHopSize() const { return hopSize; }
    /** Time from a sound to the frame that reports it, in seconds. */
    double getReportingDelaySeconds() const
    {
        return windowCentreLagInputSamples() / hostRate;
    }

    /**
     * Pitch of one window of samples at the analysis rate, as a MIDI note (0
     * when unvoiced). Exposed for tests; allocates nothing after prepare().
     */
    float detectWindow(const float *window, float &confidence, float &levelDb);

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float process(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0.0f; }
    };

    bool pushSample(float sample);
    bool analyse(LivePitchFrame &frame);
    double windowCentreLagInputSamples() const
    {
        const double smoothingLag =
            settings.medianSmoothing ? static_cast<double>(hopSize) : 0.0;
        return (0.5 * windowSize + smoothingLag) * decimation;
    }

    Settings settings;
    bool prepared = false;
    double hostRate = 44100.0;
    double analysisRate = 14700.0;
    int decimation = 1;
    int tauMin = 2;
    int tauMax = 2;
    int integration = 2;   // samples summed per lag
    int windowSize = 4;    // integration + tauMax
    int hopSize = 1;

    std::array<Biquad, 2> antiAlias;
    int decimationPhase = 0;

    std::vector<float> ring;   // power-of-two sized, decimated samples
    int ringMask = 0;
    int ringWrite = 0;
    int filled = 0;
    int sinceLastFrame = 0;
    std::int64_t streamSamples = 0;

    std::vector<float> window;
    std::vector<float> difference;
    std::vector<float> cmndf;

    // Median-of-three smoothing state: the last two raw frames.
    std::array<LivePitchFrame, 2> history{};
    int historyCount = 0;
};
