#include "../Source/Models/Project.h"
#include "../Source/Models/ProjectSerializer.h"
#include "../Source/Utils/Constants.h"
#include "TestAssert.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// A reference and a take built directly as mel spectrograms: one steady
// "vowel" (three spectral peaks) per segment, silence in between, and one note
// per segment. The take sings the same segments with other durations, so the
// note expected at any take frame is known.
namespace {

struct Segment {
    int vowel = 0;
    float midi = 0;
    int frames = 0;
    int pauseFrames = 0;
};

constexpr int kVowelBands[][3] = {
    {6, 40, 62}, {14, 31, 60}, {20, 29, 57}, {22, 26, 55}, {17, 21, 55},
    {7, 21, 50}, {18, 27, 52}, {10, 36, 56}, {12, 20, 54},
};

std::vector<Segment> referenceSegments() {
    const float notes[] = {57, 60, 64, 62, 59, 57, 55, 60, 64, 67};
    const int vowels[] = {3, 0, 5, 1, 8, 2, 6, 0, 4, 7};
    std::vector<Segment> segments;
    for (int i = 0; i < 10; ++i)
        segments.push_back({vowels[i], notes[i], 30 + 5 * (i % 3), 12 + 3 * (i % 2)});
    return segments;
}

struct Built {
    std::vector<std::vector<float>> mel;
    std::vector<float> f0;
    std::vector<std::pair<int, int>> spans; // per segment, [start, end)
};

// `semitones` transposes every note; `wrong[i]` adds to segment i's note.
Built build(const std::vector<Segment>& segments, int leadFrames, float semitones,
            const std::vector<float>& wrong = {}) {
    Built built;
    std::uint32_t state = 7;
    auto noise = [&state]() {
        state = state * 1664525u + 1013904223u;
        return 0.05f * (static_cast<float>(state >> 8) / 16777216.0f - 0.5f);
    };
    auto silence = [&](int frames) {
        for (int f = 0; f < frames; ++f) {
            std::vector<float> row(NUM_MELS);
            for (auto& value : row)
                value = -14.0f + noise();
            built.mel.push_back(std::move(row));
            built.f0.push_back(0.0f);
        }
    };

    silence(leadFrames);
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const int start = static_cast<int>(built.mel.size());
        const float midi = segment.midi + semitones + (i < wrong.size() ? wrong[i] : 0.0f);
        for (int f = 0; f < segment.frames; ++f) {
            std::vector<float> row(NUM_MELS);
            for (int band = 0; band < NUM_MELS; ++band) {
                float energy = 0.002f;
                for (int peak = 0; peak < 3; ++peak) {
                    const float x = static_cast<float>(band - kVowelBands[segment.vowel][peak]) / 3.0f;
                    energy += std::exp(-0.5f * x * x) / static_cast<float>(peak + 1);
                }
                row[static_cast<size_t>(band)] = std::log(energy) + noise();
            }
            built.mel.push_back(std::move(row));
            built.f0.push_back(midiToFreq(midi));
        }
        built.spans.emplace_back(start, static_cast<int>(built.mel.size()));
        silence(segment.pauseFrames);
    }
    return built;
}

ReferenceTrack referenceFrom(const Built& built) {
    ReferenceTrack reference;
    reference.sourcePath = "/songs/reference.wav";
    reference.features = ReferenceAlignment::computeFeatures(built.mel);
    reference.f0 = built.f0;
    return reference;
}

AudioData takeFrom(const Built& built) {
    AudioData take;
    take.melSpectrogram = built.mel;
    take.rawF0 = built.f0;
    take.f0 = built.f0;
    return take;
}

std::vector<Segment> stretched(std::vector<Segment> segments) {
    const float stretch[] = {1.4f, 0.7f, 1.2f, 0.8f, 1.3f, 0.9f, 1.1f, 0.75f, 1.25f, 1.0f};
    for (size_t i = 0; i < segments.size(); ++i) {
        segments[i].frames = static_cast<int>(static_cast<float>(segments[i].frames) * stretch[i]);
        segments[i].pauseFrames = 8 + 4 * static_cast<int>((i + 1) % 3);
    }
    return segments;
}

// The reference note shows up at the middle of every take segment, moved by
// the expected octave shift.
void checkTargets(const ReferenceTrack& reference, const Built& take,
                  const std::vector<Segment>& segments, int octaveShift) {
    CHECK(reference.isAligned());
    CHECK(reference.octaveShift == octaveShift);
    for (size_t i = 0; i < take.spans.size(); ++i) {
        const int middle = (take.spans[i].first + take.spans[i].second) / 2;
        const float expected = segments[i].midi + static_cast<float>(octaveShift);
        if (std::abs(reference.targetMidiAt(middle) - expected) > 0.01f) {
            std::fprintf(stderr, "segment %zu: target %.2f, expected %.2f\n", i,
                         reference.targetMidiAt(middle), expected);
            CHECK(false);
        }
    }
}

void guidesATakeSungAnOctaveLower() {
    const auto segments = referenceSegments();
    auto reference = referenceFrom(build(segments, 20, 0));
    const auto take = build(stretched(segments), 45, -12);
    reference.alignTo(takeFrom(take));
    checkTargets(reference, take, segments, -12);
    CHECK(reference.referenceFrameForTakeFrame.size() == take.mel.size());
    // Pauses in the take have no target.
    CHECK(reference.targetMidiAt(take.spans[0].first - 5) == 0.0f);
}

void wrongNotesDoNotMoveTheOctave() {
    const auto segments = referenceSegments();
    auto reference = referenceFrom(build(segments, 20, 0));
    const std::vector<float> wrong = {0, 3, 0, -2, 0, 0, 5, 0, -4, 0};
    const auto take = build(stretched(segments), 30, 0, wrong);
    reference.alignTo(takeFrom(take));
    checkTargets(reference, take, segments, 0);
}

void followsATakeAnOctaveHigher() {
    const auto segments = referenceSegments();
    auto reference = referenceFrom(build(segments, 20, 0));
    const auto take = build(stretched(segments), 10, 12);
    reference.alignTo(takeFrom(take));
    checkTargets(reference, take, segments, 12);
}

// The editor pads a capture's lead-in with all-zero mel rows and leaves empty
// rows in gaps between captures; both must read as silence, not singing.
void toleratesEditorPadding() {
    const auto segments = referenceSegments();
    auto reference = referenceFrom(build(segments, 20, 0));
    auto take = build(stretched(segments), 10, 0);
    const int padding = 300;
    take.mel.insert(take.mel.begin(), static_cast<size_t>(padding),
                    std::vector<float>(NUM_MELS, 0.0f));
    take.mel.insert(take.mel.begin() + padding / 2, static_cast<size_t>(40),
                    std::vector<float>{});
    take.f0.insert(take.f0.begin(), static_cast<size_t>(padding + 40), 0.0f);
    for (auto& span : take.spans) {
        span.first += padding + 40;
        span.second += padding + 40;
    }
    reference.alignTo(takeFrom(take));
    checkTargets(reference, take, segments, 0);
}

bool sameFloats(const std::vector<float>& a, const std::vector<float>& b, float tolerance) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > tolerance)
            return false;
    return true;
}

void checkSameReference(const ReferenceTrack& restored, const ReferenceTrack& saved,
                        float tolerance) {
    CHECK(restored.sourcePath == saved.sourcePath);
    CHECK(restored.octaveShift == saved.octaveShift);
    CHECK(std::abs(restored.matchCost - saved.matchCost) <= 1e-4f);
    CHECK(restored.features.numFrames == saved.features.numFrames);
    CHECK(sameFloats(restored.f0, saved.f0, tolerance));
    CHECK(sameFloats(restored.features.cepstra, saved.features.cepstra, tolerance));
    CHECK(sameFloats(restored.features.activity, saved.features.activity, tolerance));
    CHECK(sameFloats(restored.referenceFrameForTakeFrame, saved.referenceFrameForTakeFrame,
                     tolerance));
}

// Plug-in state (binary archive) and .pitchnet documents (JSON) both keep the
// reference, and loading a project without one clears the previous one.
void survivesSaveAndLoad() {
    const auto segments = referenceSegments();
    const auto take = build(stretched(segments), 30, -12);
    Project project;
    project.getAudioData() = takeFrom(take);
    project.getReference() = referenceFrom(build(segments, 20, 0));
    project.getReference().alignTo(project.getAudioData());
    CHECK(project.getReference().octaveShift == -12);
    const auto saved = project.getReference();

    juce::MemoryBlock archive;
    CHECK(ProjectSerializer::toBinaryArchive(project, archive));
    Project fromArchive;
    CHECK(ProjectSerializer::fromBinaryArchive(fromArchive, archive.getData(),
                                               archive.getSize()));
    checkSameReference(fromArchive.getReference(), saved, 0.0f);

    Project fromJson;
    CHECK(ProjectSerializer::fromJson(fromJson, ProjectSerializer::toJson(project, false, true)));
    // The JSON stores 2-3 decimals.
    checkSameReference(fromJson.getReference(), saved, 0.006f);

    Project withoutReference;
    juce::MemoryBlock plainArchive;
    CHECK(ProjectSerializer::toBinaryArchive(withoutReference, plainArchive));
    CHECK(ProjectSerializer::fromBinaryArchive(fromArchive, plainArchive.getData(),
                                               plainArchive.getSize()));
    CHECK(!fromArchive.getReference().isLoaded());
    CHECK(fromArchive.getReference().sourcePath.isEmpty());
}

void handlesMissingData() {
    ReferenceTrack empty;
    CHECK(!empty.isLoaded());
    CHECK(!empty.isAligned());
    CHECK(empty.targetMidiAt(0) == 0.0f);

    const auto segments = referenceSegments();
    auto reference = referenceFrom(build(segments, 20, 0));
    reference.alignTo(takeFrom(build(segments, 20, 0)));
    CHECK(reference.isAligned());
    CHECK(reference.targetMidiAt(-1) == 0.0f);
    CHECK(reference.targetMidiAt(1 << 20) == 0.0f);

    // A take without analysis clears the alignment rather than keeping a stale one.
    reference.alignTo(AudioData{});
    CHECK(!reference.isAligned());
    CHECK(reference.octaveShift == 0);
}

} // namespace

int main() {
    guidesATakeSungAnOctaveLower();
    wrongNotesDoNotMoveTheOctave();
    followsATakeAnOctaveHigher();
    toleratesEditorPadding();
    survivesSaveAndLoad();
    handlesMissingData();
    std::cout << "Reference track tests passed\n";
}
