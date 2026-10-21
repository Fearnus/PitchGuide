#include "../Source/Audio/Alignment/ReferenceAlignment.h"
#include "../Source/Utils/Constants.h"
#include "../Source/Utils/MelSpectrogram.h"
#include "TestAssert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <vector>

// Synthetic "singing": harmonics of f0 shaped by three vowel formants, one
// steady vowel per segment, with pauses in between. A take is the same vowel
// sequence with other durations and pitch, so the correct alignment is known:
// every sung take frame must land inside the same vowel of the reference.
namespace {

struct Formants {
    double f1, f2, f3;
};

// Average adult formant frequencies (Hz) for nine vowels.
constexpr Formants kVowels[] = {
    {280, 2250, 2890}, // i
    {550, 1770, 2490}, // E
    {690, 1660, 2490}, // ae
    {710, 1100, 2540}, // a
    {590, 880, 2540},  // O
    {310, 870, 2250},  // u
    {620, 1200, 2390}, // V
    {400, 2000, 2550}, // e
    {450, 830, 2380},  // o
};

// Fourteen segments; no vowel repeats within three steps, so any stretch of
// three or more segments occurs only once.
constexpr int kSequence[] = {3, 0, 5, 1, 8, 2, 6, 0, 4, 7, 3, 5, 1, 8};
constexpr int kNumSegments = static_cast<int>(std::size(kSequence));

struct Segment {
    int vowel = 0;
    double seconds = 0;
    double pauseAfter = 0;
};

struct Span {
    int startFrame = 0;
    int endFrame = 0; // exclusive
};

struct Signal {
    std::vector<float> audio;
    std::vector<Span> spans; // one per segment
};

constexpr double kTwoPi = 6.283185307179586;

Signal synthesize(const std::vector<Segment>& segments, double f0,
                  double leadingSilence) {
    Signal signal;
    signal.audio.assign(static_cast<size_t>(leadingSilence * SAMPLE_RATE), 0.0f);
    const int fade = SAMPLE_RATE / 50; // 20 ms

    for (const auto& segment : segments) {
        const auto& formants = kVowels[segment.vowel];
        std::vector<double> amplitudes;
        double total = 0;
        for (int h = 1; h * f0 < 8000.0; ++h) {
            const double frequency = h * f0;
            auto peak = [frequency](double center, double width) {
                const double x = (frequency - center) / width;
                return std::exp(-0.5 * x * x);
            };
            const double amplitude = peak(formants.f1, 90) +
                                     0.7 * peak(formants.f2, 120) +
                                     0.4 * peak(formants.f3, 150) + 0.01;
            amplitudes.push_back(amplitude);
            total += amplitude;
        }

        const auto start = signal.audio.size();
        const int length = static_cast<int>(segment.seconds * SAMPLE_RATE);
        for (int n = 0; n < length; ++n) {
            const double gain = std::min({1.0, double(n) / fade, double(length - n) / fade});
            double sample = 0;
            for (size_t h = 0; h < amplitudes.size(); ++h)
                sample += amplitudes[h] *
                          std::sin(kTwoPi * double(h + 1) * f0 * n / SAMPLE_RATE + 0.3 * h);
            signal.audio.push_back(static_cast<float>(0.3 * gain * sample / total));
        }
        signal.spans.push_back({secondsToFrames(float(start) / SAMPLE_RATE),
                                secondsToFrames(float(start + length) / SAMPLE_RATE)});
        signal.audio.resize(signal.audio.size() +
                                static_cast<size_t>(segment.pauseAfter * SAMPLE_RATE),
                            0.0f);
    }

    // A -70 dBFS noise floor, as any real recording has between phrases.
    std::uint32_t state = 12345;
    for (auto& sample : signal.audio) {
        state = state * 1664525u + 1013904223u;
        sample += 3e-4f * (static_cast<float>(state >> 8) / 8388608.0f - 1.0f);
    }
    return signal;
}

ReferenceAlignment::Features featuresOf(const Signal& signal) {
    MelSpectrogram mel;
    return ReferenceAlignment::computeFeatures(
        mel.compute(signal.audio.data(), static_cast<int>(signal.audio.size())));
}

std::vector<Segment> referenceSegments() {
    std::vector<Segment> segments;
    for (int i = 0; i < kNumSegments; ++i)
        segments.push_back({kSequence[i], 0.35 + 0.05 * (i % 4), 0.15 + 0.05 * (i % 3)});
    return segments;
}

// The take sings segments [first, last) with its own timing: each vowel held
// 0.6x..1.5x as long as in the reference, and different pauses.
std::vector<Segment> takeSegments(int first, int last) {
    constexpr double kStretch[] = {1.4, 0.7, 1.2, 0.6, 1.5, 0.9, 1.3, 0.8};
    std::vector<Segment> segments = referenceSegments();
    std::vector<Segment> take;
    for (int i = first; i < last; ++i) {
        auto segment = segments[static_cast<size_t>(i)];
        segment.seconds *= kStretch[i % 8];
        segment.pauseAfter = 0.1 + 0.08 * ((i + 1) % 3);
        take.push_back(segment);
    }
    return take;
}

// Every frame of every sung take segment (minus the 20 ms fades) must map into
// the matching reference segment, and the mapping must never run backwards.
void checkSegmentsMatch(const ReferenceAlignment::Result& result,
                        const Signal& take, const Signal& reference,
                        int firstReferenceSegment) {
    CHECK(result.valid);
    CHECK(result.referenceFrame.size() >=
          static_cast<size_t>(take.spans.back().endFrame));
    for (size_t i = 1; i < result.referenceFrame.size(); ++i)
        CHECK(result.referenceFrame[i] >= result.referenceFrame[i - 1]);

    constexpr int kFadeFrames = 2;
    for (size_t s = 0; s < take.spans.size(); ++s) {
        const auto& takeSpan = take.spans[s];
        const auto& referenceSpan =
            reference.spans[static_cast<size_t>(firstReferenceSegment) + s];
        for (int frame = takeSpan.startFrame + kFadeFrames;
             frame < takeSpan.endFrame - kFadeFrames; ++frame) {
            const float mapped = result.referenceFrame[static_cast<size_t>(frame)];
            if (mapped < referenceSpan.startFrame - kFadeFrames ||
                mapped > referenceSpan.endFrame + kFadeFrames) {
                std::fprintf(stderr,
                             "take segment %zu frame %d -> reference frame %.1f, "
                             "expected %d..%d\n",
                             s, frame, mapped, referenceSpan.startFrame,
                             referenceSpan.endFrame);
                CHECK(false);
            }
        }
    }
}

void alignsSignalToItself() {
    const auto reference = synthesize(referenceSegments(), 220.0, 0.3);
    const auto features = featuresOf(reference);
    const auto result = ReferenceAlignment::align(features, features);
    CHECK(result.valid);
    CHECK(result.referenceStart == 0);
    CHECK(result.referenceEnd == features.numFrames);
    for (int frame = 0; frame < features.numFrames; ++frame)
        CHECK(std::abs(result.referenceFrame[static_cast<size_t>(frame)] - frame) <= 0.5f);
    CHECK(result.meanActiveCost < 0.01f);
}

void followsTempoChangesAnOctaveLower() {
    const auto reference = synthesize(referenceSegments(), 220.0, 0.3);
    const auto take = synthesize(takeSegments(0, kNumSegments), 110.0, 0.8);
    const auto result =
        ReferenceAlignment::align(featuresOf(take), featuresOf(reference));
    checkSegmentsMatch(result, take, reference, 0);
}

void findsAVerseInsideTheReference() {
    const auto reference = synthesize(referenceSegments(), 220.0, 0.3);
    const auto take = synthesize(takeSegments(5, 10), 246.9, 0.5);
    const auto result =
        ReferenceAlignment::align(featuresOf(take), featuresOf(reference));
    checkSegmentsMatch(result, take, reference, 5);
    // The take's leading silence lands in the pause before segment 5.
    CHECK(result.referenceStart >= reference.spans[4].endFrame - 2);
    CHECK(result.referenceStart <= reference.spans[5].startFrame + 2);
    CHECK(result.referenceEnd >= reference.spans[9].endFrame - 2);
    CHECK(result.referenceEnd <= reference.spans[10].startFrame + 2);
}

void rejectsEmptyInput() {
    const auto features = featuresOf(synthesize(referenceSegments(), 220.0, 0.3));
    CHECK(!ReferenceAlignment::align({}, features).valid);
    CHECK(!ReferenceAlignment::align(features, {}).valid);
    CHECK(ReferenceAlignment::computeFeatures({}).numFrames == 0);
}

} // namespace

int main() {
    alignsSignalToItself();
    followsTempoChangesAnOctaveLower();
    findsAVerseInsideTheReference();
    rejectsEmptyInput();
    std::cout << "Reference alignment tests passed\n";
}
