#include "KaraokeReference.h"

#include "../../Models/Project.h"
#include "../../Utils/Constants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr int kFormatMagic = 0x504c5246; // "PLRF"
constexpr int kFormatVersion = 1;
constexpr double kAnalysisFrameSeconds =
    static_cast<double>(HOP_SIZE) / static_cast<double>(SAMPLE_RATE);

/** Sorts notes by start and makes them one line: a later note wins where
    two overlap, so noteAt() has one answer. */
void makeMonophonic(std::vector<KaraokeReference::Note> &notes)
{
    using Note = KaraokeReference::Note;
    std::sort(notes.begin(), notes.end(),
              [](const Note &a, const Note &b) { return a.startSeconds < b.startSeconds; });
    for (size_t i = 1; i < notes.size(); ++i)
        notes[i - 1].endSeconds = std::min(notes[i - 1].endSeconds, notes[i].startSeconds);
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [](const Note &n) { return n.endSeconds <= n.startSeconds; }),
                notes.end());
}

/** Notes of one track and channel of a MIDI file. */
struct MidiPart
{
    juce::String trackName;
    int channel = 0;
    std::vector<KaraokeReference::Note> notes;
};

bool soundsLikeTheTune(const juce::String &trackName)
{
    for (const char *word : {"vocal", "voice", "vox", "sing", "melody", "lead", "tune"})
        if (trackName.containsIgnoreCase(word))
            return true;
    return false;
}

/** Notes starting this close together are one chord. */
constexpr double kChordSeconds = 0.01;
/** The longest MIDI file read: a melody is a few kilobytes. */
constexpr juce::int64 kMaxMidiBytes = 16 * 1024 * 1024;
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

    makeMonophonic(reference.notes);
    return reference;
}

bool KaraokeReference::isMidiFile(const juce::File &file)
{
    return file.hasFileExtension("mid;midi");
}

std::optional<KaraokeReference> KaraokeReference::fromMidi(juce::InputStream &stream,
                                                           const juce::String &sourcePath,
                                                           juce::String *error)
{
    const auto fileName = juce::File::isAbsolutePath(sourcePath)
                              ? juce::File(sourcePath).getFileName()
                              : sourcePath;
    auto fail = [error](const juce::String &message) -> std::optional<KaraokeReference> {
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    };

    juce::MidiFile midi;
    // Matching note-offs are added for notes the file never ends, at the end
    // of their track.
    if (!midi.readFrom(stream, true) || midi.getNumTracks() == 0)
        return fail("Could not read " + fileName + " as a MIDI file");
    // Ticks to seconds through every tempo change in the file. A file without
    // any is at 120 bpm, as the MIDI standard says.
    midi.convertTimestampTicksToSeconds();

    std::vector<MidiPart> parts;
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        const auto *track = midi.getTrack(trackIndex);
        juce::String trackName;
        for (int i = 0; i < track->getNumEvents() && trackName.isEmpty(); ++i)
        {
            const auto &message = track->getEventPointer(i)->message;
            if (message.isTrackNameEvent())
                trackName = message.getTextFromTextMetaEvent();
        }

        // A type 0 file keeps every instrument in one track, by channel.
        std::array<MidiPart, 16> byChannel;
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto *event = track->getEventPointer(i);
            const auto &message = event->message;
            if (!message.isNoteOn() || event->noteOffObject == nullptr)
                continue;
            const int channel = message.getChannel();
            if (channel < 1 || channel > 16 || channel == 10) // GM drums
                continue;
            Note note;
            note.startSeconds = message.getTimeStamp();
            note.endSeconds = event->noteOffObject->message.getTimeStamp();
            note.midi = static_cast<float>(message.getNoteNumber());
            if (note.endSeconds > note.startSeconds)
                byChannel[static_cast<size_t>(channel - 1)].notes.push_back(note);
        }
        for (int channel = 1; channel <= 16; ++channel)
        {
            auto &part = byChannel[static_cast<size_t>(channel - 1)];
            if (part.notes.empty())
                continue;
            part.trackName = trackName;
            part.channel = channel;
            parts.push_back(std::move(part));
        }
    }
    if (parts.empty())
        return fail("No notes were found in " + fileName);

    // The part named like the tune, else the busiest one.
    const auto best = std::max_element(parts.begin(), parts.end(),
                                       [](const MidiPart &a, const MidiPart &b) {
                                           const bool aTune = soundsLikeTheTune(a.trackName);
                                           const bool bTune = soundsLikeTheTune(b.trackName);
                                           if (aTune != bTune)
                                               return bTune;
                                           return a.notes.size() < b.notes.size();
                                       });
    auto notes = std::move(best->notes);

    // A chord is sung as its top note.
    std::sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) {
        return a.startSeconds < b.startSeconds;
    });
    std::vector<Note> melody;
    for (size_t i = 0; i < notes.size();)
    {
        size_t top = i, next = i + 1;
        for (; next < notes.size() &&
               notes[next].startSeconds - notes[i].startSeconds < kChordSeconds;
             ++next)
            if (notes[next].midi > notes[top].midi)
                top = next;
        melody.push_back(notes[top]);
        i = next;
    }

    KaraokeReference reference;
    reference.sourcePath = sourcePath;
    reference.frameSeconds = kAnalysisFrameSeconds;
    reference.notes = std::move(melody);
    makeMonophonic(reference.notes);
    if (!reference.isLoaded())
        return fail("No notes were found in " + fileName);
    return reference;
}

std::optional<KaraokeReference> KaraokeReference::fromMidiFile(const juce::File &file,
                                                               juce::String *error)
{
    if (!file.existsAsFile() || file.getSize() > kMaxMidiBytes)
    {
        if (error != nullptr)
            *error = "Could not read " + file.getFileName();
        return std::nullopt;
    }
    juce::FileInputStream stream(file);
    if (!stream.openedOk())
    {
        if (error != nullptr)
            *error = "Could not open " + file.getFileName();
        return std::nullopt;
    }
    return fromMidi(stream, file.getFullPathName(), error);
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
