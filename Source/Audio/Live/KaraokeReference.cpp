#include "KaraokeReference.h"

#include "../../Models/Project.h"
#include "../../Utils/Constants.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr int kFormatMagic = 0x504c5246; // "PLRF"
constexpr int kFormatVersion = 1;
constexpr double kAnalysisFrameSeconds =
    static_cast<double>(HOP_SIZE) / static_cast<double>(SAMPLE_RATE);
} // namespace

double KaraokeReference::getDurationSeconds() const
{
    double end = notes.empty() ? 0.0 : notes.back().endSeconds;
    return std::max(end, static_cast<double>(curveMidi.size()) * frameSeconds);
}

size_t KaraokeReference::firstNoteEndingAfter(double referenceSeconds) const
{
    const auto it = std::upper_bound(
        notes.begin(), notes.end(), referenceSeconds,
        [](double time, const Note &note) { return time < note.endSeconds; });
    return static_cast<size_t>(std::distance(notes.begin(), it));
}

const KaraokeReference::Note *KaraokeReference::noteAt(double referenceSeconds) const
{
    const size_t index = firstNoteEndingAfter(referenceSeconds);
    if (index >= notes.size())
        return nullptr;
    const auto &note = notes[index];
    return referenceSeconds >= note.startSeconds ? &note : nullptr;
}

float KaraokeReference::curveAt(double referenceSeconds) const
{
    if (frameSeconds <= 0.0 || referenceSeconds < 0.0)
        return 0.0f;
    const auto index = static_cast<size_t>(std::lround(referenceSeconds / frameSeconds));
    return index < curveMidi.size() ? curveMidi[index] : 0.0f;
}

std::optional<float> KaraokeReference::pitchErrorAt(double referenceSeconds, float sungMidi,
                                                    float transpose) const
{
    const auto *note = noteAt(referenceSeconds);
    if (note == nullptr)
        return std::nullopt;
    float error = sungMidi - (note->midi + transpose);
    const float curve = curveAt(referenceSeconds);
    if (curve > 0.0f)
    {
        const float curveError = sungMidi - (curve + transpose);
        if (std::abs(curveError) < std::abs(error))
            error = curveError;
    }
    return error;
}

KaraokeReference KaraokeReference::fromProject(const Project &project,
                                               const juce::String &path)
{
    KaraokeReference reference;
    reference.sourcePath = path;
    reference.frameSeconds = kAnalysisFrameSeconds;

    const auto &audio = project.getAudioData();
    const auto &f0 = audio.f0;
    reference.curveMidi.assign(f0.size(), 0.0f);

    for (const auto &note : project.getNotes())
    {
        if (note.isRest() || note.getEndFrame() <= note.getStartFrame())
            continue;
        Note entry;
        entry.startSeconds = note.getStartFrame() * kAnalysisFrameSeconds;
        entry.endSeconds = note.getEndFrame() * kAnalysisFrameSeconds;
        entry.midi = note.getAdjustedMidiNote();
        reference.notes.push_back(entry);

        const int first = std::max(0, note.getStartFrame());
        const int last = std::min(note.getEndFrame(), static_cast<int>(f0.size()));
        for (int frame = first; frame < last; ++frame)
        {
            const float hz = f0[static_cast<size_t>(frame)];
            reference.curveMidi[static_cast<size_t>(frame)] = hz > 0.0f ? freqToMidi(hz) : 0.0f;
        }
    }

    std::sort(reference.notes.begin(), reference.notes.end(),
              [](const Note &a, const Note &b) { return a.startSeconds < b.startSeconds; });
    // A later note wins where two touch, so noteAt() has one answer.
    for (size_t i = 1; i < reference.notes.size(); ++i)
        reference.notes[i - 1].endSeconds =
            std::min(reference.notes[i - 1].endSeconds, reference.notes[i].startSeconds);
    reference.notes.erase(
        std::remove_if(reference.notes.begin(), reference.notes.end(),
                       [](const Note &n) { return n.endSeconds <= n.startSeconds; }),
        reference.notes.end());
    return reference;
}

juce::MemoryBlock KaraokeReference::toMemoryBlock() const
{
    juce::MemoryOutputStream out;
    out.writeInt(kFormatMagic);
    out.writeInt(kFormatVersion);
    out.writeString(sourcePath);
    out.writeDouble(frameSeconds);
    out.writeInt(static_cast<int>(notes.size()));
    for (const auto &note : notes)
    {
        out.writeDouble(note.startSeconds);
        out.writeDouble(note.endSeconds);
        out.writeFloat(note.midi);
    }
    out.writeInt(static_cast<int>(curveMidi.size()));
    for (float value : curveMidi)
        out.writeFloat(value);
    return out.getMemoryBlock();
}

std::optional<KaraokeReference> KaraokeReference::fromMemoryBlock(const juce::MemoryBlock &data)
{
    juce::MemoryInputStream in(data, false);
    if (in.getTotalLength() < 8 || in.readInt() != kFormatMagic)
        return std::nullopt;
    if (in.readInt() != kFormatVersion)
        return std::nullopt;

    KaraokeReference reference;
    reference.sourcePath = in.readString();
    reference.frameSeconds = in.readDouble();

    const int noteCount = in.readInt();
    constexpr int kNoteBytes = 8 + 8 + 4;
    if (noteCount < 0 || in.getNumBytesRemaining() < static_cast<juce::int64>(noteCount) * kNoteBytes)
        return std::nullopt;
    reference.notes.resize(static_cast<size_t>(noteCount));
    for (auto &note : reference.notes)
    {
        note.startSeconds = in.readDouble();
        note.endSeconds = in.readDouble();
        note.midi = in.readFloat();
    }

    const int curveCount = in.readInt();
    if (curveCount < 0 || in.getNumBytesRemaining() < static_cast<juce::int64>(curveCount) * 4)
        return std::nullopt;
    reference.curveMidi.resize(static_cast<size_t>(curveCount));
    for (auto &value : reference.curveMidi)
        value = in.readFloat();

    // Plain range check: -ffast-math builds drop isfinite().
    if (!(reference.frameSeconds > 1.0e-4 && reference.frameSeconds < 1.0))
        return std::nullopt;
    return reference;
}
