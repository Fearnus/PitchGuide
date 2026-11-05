// PitchGuide Live: the real-time detector, the sung trail and its score, and
// the reference notes the karaoke view draws.

#include "../Source/Audio/Live/KaraokeReference.h"
#include "../Source/Audio/Live/LivePitchDetector.h"
#include "../Source/Audio/Live/SungPitchTrail.h"
#include "../Source/Models/Project.h"
#include "../Source/Utils/Constants.h"
#include "TestAssert.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

float hzToMidi(double hz) { return static_cast<float>(69.0 + 12.0 * std::log2(hz / 440.0)); }

/** A voice-like harmonic tone (8 partials, falling 1/k). */
std::vector<float> harmonicTone(double sampleRate, double seconds,
                                const std::function<double(double)> &hzAt,
                                float amplitude = 0.25f)
{
    std::vector<float> out(static_cast<size_t>(seconds * sampleRate), 0.0f);
    double phase = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double hz = hzAt(t);
        if (hz <= 0.0)
        {
            out[i] = 0.0f;
            continue;
        }
        phase += 2.0 * kPi * hz / sampleRate;
        double value = 0.0;
        for (int k = 1; k <= 8; ++k)
            if (hz * k < sampleRate * 0.45)
                value += std::sin(phase * k) / k;
        out[i] = static_cast<float>(amplitude * value * 0.6);
    }
    return out;
}

/** Feeds a signal the way a host does: fixed blocks, host time advancing. */
std::vector<LivePitchFrame> run(LivePitchDetector &detector, const std::vector<float> &signal,
                                double sampleRate, double hostStartSeconds = 0.0,
                                int blockSize = 256)
{
    std::vector<LivePitchFrame> frames;
    for (size_t start = 0; start < signal.size(); start += static_cast<size_t>(blockSize))
    {
        const int count = static_cast<int>(std::min<size_t>(blockSize, signal.size() - start));
        detector.process(signal.data() + start, count,
                         hostStartSeconds + static_cast<double>(start) / sampleRate, true,
                         [&frames](const LivePitchFrame &frame) { frames.push_back(frame); });
    }
    return frames;
}

void testSteadyTonesAcrossRatesAndRange()
{
    const double rates[] = {44100.0, 48000.0, 96000.0, 22050.0};
    const double pitches[] = {70.0, 82.41, 110.0, 196.0, 261.63, 440.0, 659.25, 987.77};
    for (double rate : rates)
    {
        for (double hz : pitches)
        {
            LivePitchDetector detector;
            detector.prepare(rate);
            const auto signal = harmonicTone(rate, 0.6, [hz](double) { return hz; });
            const auto frames = run(detector, signal, rate);

            int considered = 0;
            int voiced = 0;
            std::vector<float> errors;
            for (const auto &frame : frames)
            {
                if (frame.hostSeconds < 0.1 || frame.hostSeconds > 0.55)
                    continue;
                ++considered;
                if (!frame.isVoiced())
                    continue;
                ++voiced;
                errors.push_back(std::abs(frame.midi - hzToMidi(hz)) * 100.0f);
            }
            CHECK(considered > 50);
            CHECK(voiced >= considered * 95 / 100);
            std::sort(errors.begin(), errors.end());
            const float median = errors[errors.size() / 2];
            const float worst = errors.back();
            if (median > 5.0f || worst > 25.0f)
                std::fprintf(stderr, "rate %.0f, %.2f Hz: median %.2f c, worst %.2f c\n", rate,
                             hz, median, worst);
            CHECK(median <= 5.0f);
            CHECK(worst <= 25.0f);
        }
    }
}

void testLowLatency()
{
    LivePitchDetector detector;
    detector.prepare(48000.0);
    // Half a window plus the smoothing hop: the frame for a sound arrives
    // well under 40 ms after it was sung.
    CHECK(detector.getReportingDelaySeconds() < 0.035);
    CHECK(detector.getAnalysisSampleRate() >= 14000.0);
    CHECK(detector.getHopSize() > 0);
}

void testSilenceAndNoiseAreUnvoiced()
{
    const double rate = 48000.0;
    {
        LivePitchDetector detector;
        detector.prepare(rate);
        const std::vector<float> silence(static_cast<size_t>(rate), 0.0f);
        for (const auto &frame : run(detector, silence, rate))
            CHECK(!frame.isVoiced());
    }
    {
        // A tone under the gate is ignored.
        LivePitchDetector detector;
        detector.prepare(rate);
        detector.setGateDb(-30.0f);
        const auto quiet = harmonicTone(rate, 0.5, [](double) { return 220.0; }, 0.005f);
        for (const auto &frame : run(detector, quiet, rate))
            CHECK(!frame.isVoiced());
    }
    {
        LivePitchDetector detector;
        detector.prepare(rate);
        std::mt19937 random(1234);
        std::normal_distribution<float> gaussian(0.0f, 0.1f);
        std::vector<float> noise(static_cast<size_t>(rate));
        for (auto &sample : noise)
            sample = gaussian(random);
        int voiced = 0;
        const auto frames = run(detector, noise, rate);
        for (const auto &frame : frames)
            voiced += frame.isVoiced() ? 1 : 0;
        CHECK(voiced * 20 <= static_cast<int>(frames.size())); // <= 5 %
    }
}

void testTimestampsAreWhenItWasSung()
{
    // Silence, then 220 Hz from 1.0 s, then 330 Hz from 1.5 s, starting at
    // host time 10 s: the frames must put both changes where they happened.
    const double rate = 44100.0;
    const double hostStart = 10.0;
    LivePitchDetector detector;
    detector.prepare(rate);
    const auto signal = harmonicTone(rate, 2.0, [](double t) {
        return t < 1.0 ? 0.0 : (t < 1.5 ? 220.0 : 330.0);
    });
    const auto frames = run(detector, signal, rate, hostStart, 512);

    double firstVoiced = -1.0;
    for (const auto &frame : frames)
    {
        CHECK(std::abs((frame.hostSeconds - hostStart) - frame.streamSeconds) < 1.0e-6);
        if (frame.isVoiced() && firstVoiced < 0.0)
            firstVoiced = frame.hostSeconds - hostStart;
    }
    CHECK(firstVoiced > 0.98 && firstVoiced < 1.04);

    for (const auto &frame : frames)
    {
        const double t = frame.hostSeconds - hostStart;
        if (t > 1.1 && t < 1.47)
            CHECK(frame.isVoiced() && std::abs(frame.midi - hzToMidi(220.0)) < 0.2f);
        if (t > 1.53 && t < 1.95)
            CHECK(frame.isVoiced() && std::abs(frame.midi - hzToMidi(330.0)) < 0.2f);
    }

    // Frames come every hop, in order.
    for (size_t i = 1; i < frames.size(); ++i)
    {
        const double step = frames[i].hostSeconds - frames[i - 1].hostSeconds;
        CHECK(step > 0.004 && step < 0.006);
    }
}

LivePitchFrame frameAt(double seconds, float midi, bool playing = true)
{
    LivePitchFrame frame;
    frame.hostSeconds = seconds;
    frame.streamSeconds = seconds;
    frame.midi = midi;
    frame.hostPlaying = playing;
    return frame;
}

void testTrailRecordsOverwritesAndScores()
{
    SungPitchTrail trail;
    CHECK(trail.isEmpty());

    // Stopped transport records nothing.
    trail.add(frameAt(1.0, 60.0f, false));
    CHECK(trail.isEmpty());

    // One pass over 1..2 s: first half at 60.2, second half at 61.5.
    for (double t = 1.0; t < 2.0; t += 0.005)
        trail.add(frameAt(t, t < 1.5 ? 60.2f : 61.5f));
    CHECK(!trail.isEmpty());
    CHECK(std::abs(trail.slotValue(SungPitchTrail::slotAt(1.2)) - 60.2f) < 1.0e-4f);
    CHECK(std::abs(trail.slotValue(SungPitchTrail::slotAt(1.8)) - 61.5f) < 1.0e-4f);
    CHECK(std::isnan(trail.slotValue(SungPitchTrail::slotAt(0.5))));

    KaraokeReference reference;
    reference.frameSeconds = static_cast<double>(HOP_SIZE) / SAMPLE_RATE;
    reference.notes.push_back({1.0, 2.0, 60.0f});

    auto score = trail.score(reference, 0.0, 0.0f, 50.0f);
    CHECK(std::abs(score.noteSeconds - 1.0) < 0.02);
    CHECK(std::abs(score.voicedSeconds - 1.0) < 0.02);
    CHECK(std::abs(score.accuracy() - 0.5f) < 0.02f);

    // Transposing the reference up 1.5 st makes the other half the in-tune one.
    score = trail.score(reference, 0.0, 1.5f, 50.0f);
    CHECK(std::abs(score.accuracy() - 0.5f) < 0.02f);
    // Both halves are 65 cents from a target of 60.85.
    score = trail.score(reference, 0.0, 0.85f, 70.0f);
    CHECK(std::abs(score.accuracy() - 1.0f) < 0.02f);

    // Copying the reference's own pitch counts as in tune even where it is
    // away from the note: here it slides up to 61.5 in the second half.
    KaraokeReference sliding = reference;
    sliding.curveMidi.assign(static_cast<size_t>(2.5 / sliding.frameSeconds), 0.0f);
    for (size_t frame = 0; frame < sliding.curveMidi.size(); ++frame)
    {
        const double t = static_cast<double>(frame) * sliding.frameSeconds;
        if (t >= 1.0 && t < 2.0)
            sliding.curveMidi[frame] = t < 1.5 ? 60.0f : 61.5f;
    }
    score = trail.score(sliding, 0.0, 0.0f, 50.0f);
    CHECK(score.accuracy() > 0.98f);
    CHECK(std::abs(sliding.pitchErrorAt(1.8, 61.4f, 0.0f) + 0.1f) < 1.0e-4f);
    CHECK(std::abs(sliding.pitchErrorAt(1.2, 60.3f, 0.0f) - 0.3f) < 1.0e-4f);
    CHECK(std::isnan(sliding.pitchErrorAt(2.2, 60.0f, 0.0f)));

    // A reference placed 10 s later has nothing sung under it.
    score = trail.score(reference, 10.0, 0.0f, 50.0f);
    CHECK(score.noteSeconds == 0.0 && score.accuracy() == 0.0f);

    // Looping the passage overwrites the first attempt, slot by slot.
    for (double t = 1.0; t < 2.0; t += 0.005)
        trail.add(frameAt(t, 60.0f));
    score = trail.score(reference, 0.0, 0.0f, 50.0f);
    CHECK(std::abs(score.accuracy() - 1.0f) < 0.01f);

    // Small gaps between frames are filled, a seek is not.
    trail.clear();
    trail.add(frameAt(5.000, 62.0f));
    trail.add(frameAt(5.012, 62.0f));
    CHECK(std::abs(trail.slotValue(SungPitchTrail::slotAt(5.006)) - 62.0f) < 1.0e-4f);
    trail.add(frameAt(7.0, 64.0f));
    CHECK(std::isnan(trail.slotValue(SungPitchTrail::slotAt(6.0))));

    // Latency compensation moves the voice earlier.
    trail.clear();
    trail.add(frameAt(3.0, 65.0f), 0.1);
    CHECK(std::abs(trail.slotValue(SungPitchTrail::slotAt(2.9)) - 65.0f) < 1.0e-4f);

    // Unvoiced frames count as sung nothing, not as no data.
    trail.add(frameAt(4.0, 0.0f));
    CHECK(trail.slotValue(SungPitchTrail::slotAt(4.0)) == 0.0f);
}

void testReferenceFromProjectAndState()
{
    Project project;
    auto &audio = project.getAudioData();
    audio.f0.assign(400, 0.0f);
    for (int frame = 50; frame < 150; ++frame)
        audio.f0[static_cast<size_t>(frame)] = midiToFreq(62.0f);
    for (int frame = 200; frame < 300; ++frame)
        audio.f0[static_cast<size_t>(frame)] = midiToFreq(65.0f);

    project.addNote(Note(200, 300, 65.0f));
    project.addNote(Note(50, 150, 62.0f));
    Note rest(150, 200, 0.0f);
    rest.setRest(true);
    project.addNote(rest);

    const auto reference = KaraokeReference::fromProject(project, "C:/songs/vocal.wav");
    CHECK(reference.isLoaded());
    CHECK(reference.notes.size() == 2);
    const double frame = static_cast<double>(HOP_SIZE) / SAMPLE_RATE;
    CHECK(std::abs(reference.notes[0].startSeconds - 50 * frame) < 1.0e-9);
    CHECK(reference.notes[0].midi == 62.0f);
    CHECK(reference.notes[1].midi == 65.0f);
    CHECK(std::abs(reference.curveAt(100 * frame) - 62.0f) < 1.0e-3f);
    CHECK(reference.curveAt(175 * frame) == 0.0f);

    const auto *note = reference.noteAt(250 * frame);
    CHECK(note != nullptr && note->midi == 65.0f);
    CHECK(reference.noteAt(175 * frame) == nullptr);
    CHECK(reference.noteAt(10.0) == nullptr);
    CHECK(reference.firstNoteEndingAfter(0.0) == 0);
    CHECK(reference.firstNoteEndingAfter(160 * frame) == 1);

    const auto restored = KaraokeReference::fromMemoryBlock(reference.toMemoryBlock());
    CHECK(restored.has_value());
    CHECK(restored->sourcePath == reference.sourcePath);
    CHECK(restored->notes.size() == reference.notes.size());
    CHECK(restored->notes[1].endSeconds == reference.notes[1].endSeconds);
    CHECK(restored->curveMidi == reference.curveMidi);

    juce::MemoryBlock garbage("not a reference", 15);
    CHECK(!KaraokeReference::fromMemoryBlock(garbage).has_value());
}
} // namespace

int main()
{
    testSteadyTonesAcrossRatesAndRange();
    testLowLatency();
    testSilenceAndNoiseAreUnvoiced();
    testTimestampsAreWhenItWasSung();
    testTrailRecordsOverwritesAndScores();
    testReferenceFromProjectAndState();
    std::printf("LivePitchTests passed\n");
    return 0;
}
