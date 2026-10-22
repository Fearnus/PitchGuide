#include "ReferenceMatch.h"

#include "PitchCurveProcessor.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace ReferenceMatch
{
std::vector<NoteState> capture(const std::vector<Note *> &notes)
{
    std::vector<NoteState> states;
    states.reserve(notes.size());
    for (auto *note : notes)
        if (note != nullptr)
            states.push_back({note, NotePitchState::of(*note)});
    return states;
}

std::vector<NoteState> bases(const Project &project, const std::vector<Note *> &notes)
{
    auto states = capture(notes);
    const auto &memory = project.getReferenceMatch();
    for (auto &state : states)
        if (const auto *before = memory.pitchBeforeMatching(*state.note))
            state.pitch = *before;
    return states;
}

namespace
{
std::vector<NotePitchState> currentPitch(const std::vector<NoteState> &states)
{
    std::vector<NotePitchState> pitch;
    pitch.reserve(states.size());
    for (const auto &state : states)
        pitch.push_back(NotePitchState::of(*state.note));
    return pitch;
}

// Marks the notes whose pitch is no longer `previous` for resynthesis, and
// returns how many changed by more than the saved precision: a project
// reloaded from JSON rematches only to within it, which is not worth a
// render or an undo step. Notes keep their frames while matching.
int markChanged(Project &project, const std::vector<NoteState> &states,
                const std::vector<NotePitchState> &previous)
{
    int first = std::numeric_limits<int>::max(), last = -1, changed = 0;
    for (size_t i = 0; i < states.size(); ++i)
    {
        auto *note = states[i].note;
        const auto now = NotePitchState::of(*note);
        if (now.isCloseTo(previous[i], 0.0f))
            continue;
        note->markDirty();
        note->markSynthDirty();
        first = std::min(first, note->getStartFrame());
        last = std::max(last, note->getEndFrame());
        if (!now.isCloseTo(previous[i]))
            ++changed;
    }
    project.markNoteEditDirtyRange(first, last);
    return changed;
}
} // namespace

int restore(Project &project, const std::vector<NoteState> &states)
{
    const auto previous = currentPitch(states);
    for (const auto &state : states)
        state.pitch.applyTo(*state.note);
    PitchCurveProcessor::rebuildBaseFromNotes(project);
    return markChanged(project, states, previous);
}

std::optional<float> referencePitchOver(const ReferenceTrack &reference,
                                        int startFrame, int endFrame,
                                        float transpose)
{
    std::vector<float> sung;
    for (int frame = startFrame; frame < endFrame; ++frame)
        if (const float midi = reference.targetMidiAt(frame); midi > 0.0f)
            sung.push_back(midi);
    const int length = endFrame - startFrame;
    if (length <= 0 || static_cast<int>(sung.size()) * 2 < length)
        return std::nullopt;

    // The median ignores the slides into and out of the note.
    const auto middle = sung.begin() + static_cast<std::ptrdiff_t>(sung.size() / 2);
    std::nth_element(sung.begin(), middle, sung.end());
    return *middle + transpose;
}

Outcome apply(Project &project, const std::vector<NoteState> &bases,
              const Settings &settings)
{
    const auto previous = currentPitch(bases);
    for (const auto &base : bases)
        base.pitch.applyTo(*base.note);

    const auto &reference = project.getReference();
    struct Matched
    {
        Note *note;
        float referencePitch;
    };
    std::vector<Matched> matched;
    if (reference.isAligned())
    {
        for (const auto &base : bases)
        {
            auto *note = base.note;
            if (note->isRest())
                continue;
            const auto target = referencePitchOver(
                reference, note->getStartFrame(), note->getEndFrame(), settings.transpose);
            if (!target)
                continue;
            matched.push_back({note, *target});
            note->setMidiNote(base.pitch.midiNote +
                              settings.notePitch * (*target - base.pitch.midiNote));
        }
    }
    PitchCurveProcessor::rebuildBaseFromNotes(project);

    if (settings.expression > 0.0f && !matched.empty())
    {
        // Bake each note's new contour: its current curve, moved part of the
        // way to the reference's curve carried to the note's new centre. The
        // contour then already holds the result, so the note's contour tools
        // are reset (as a pitch drawing does) rather than applied on top.
        const auto &audioData = project.getAudioData();
        for (const auto &[note, referencePitch] : matched)
        {
            const int start = note->getStartFrame();
            const int length = std::max(0, note->getEndFrame() - start);
            std::vector<float> baked(static_cast<size_t>(length), 0.0f);
            for (int i = 0; i < length; ++i)
            {
                const auto frame = static_cast<size_t>(start + i);
                if (frame >= audioData.basePitch.size() || frame >= audioData.deltaPitch.size())
                    break;
                const float base = audioData.basePitch[frame];
                float curve = base + audioData.deltaPitch[frame];
                if (const float sung = reference.targetMidiAt(start + i); sung > 0.0f)
                {
                    const float guide = note->getMidiNote() +
                                        (sung + settings.transpose - referencePitch);
                    curve += settings.expression * (guide - curve);
                }
                baked[static_cast<size_t>(i)] = curve - base;
            }
            note->setTiltLeft(0.0f);
            note->setTiltRight(0.0f);
            note->setVibrato(1.0f);
            note->setPitchDrift(1.0f);
            note->setSmoothLeftFrames(0);
            note->setSmoothRightFrames(0);
            note->setDeltaScale(1.0f);
            note->setDeltaOffset(0.0f);
            note->setBakedDeltaPitch(std::move(baked));
        }
        PitchCurveProcessor::rebuildBaseFromNotes(project);
    }

    Outcome outcome;
    outcome.matched = static_cast<int>(matched.size());
    outcome.changed = markChanged(project, bases, previous);
    return outcome;
}

void remember(Project &project, const std::vector<NoteState> &bases,
              const Settings &settings)
{
    auto &memory = project.getReferenceMatch();
    memory.settings = settings;
    auto &entries = memory.entries;
    for (const auto &base : bases)
    {
        const auto &note = *base.note;
        const auto existing = std::find_if(entries.begin(), entries.end(), [&note](const auto &entry) {
            return entry.startFrame == note.getStartFrame() && entry.endFrame == note.getEndFrame();
        });
        auto now = NotePitchState::of(note);
        if (now.isCloseTo(base.pitch))
        {
            // Not matched (anymore): it starts from its current pitch anyway.
            if (existing != entries.end())
                entries.erase(existing);
            continue;
        }
        ReferenceMatchMemory::Entry entry{note.getStartFrame(), note.getEndFrame(),
                                          base.pitch, std::move(now)};
        if (existing != entries.end())
            *existing = std::move(entry);
        else
            entries.push_back(std::move(entry));
    }

    // Forget notes that were deleted, split or merged since they were matched.
    const auto &notes = project.getNotes();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&notes](const auto &entry) {
        return std::none_of(notes.begin(), notes.end(), [&entry](const Note &note) {
            return note.getStartFrame() == entry.startFrame && note.getEndFrame() == entry.endFrame;
        });
    }), entries.end());
}
} // namespace ReferenceMatch
