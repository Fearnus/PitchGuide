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
            states.push_back({note, TransformParams::fromNote(*note),
                              note->getBakedDeltaPitch(),
                              note->getLastNonMacroMidiNote()});
    return states;
}

namespace
{
// Notes keep their frames while matching, so this covers every curve change.
void markChanged(Project &project, const std::vector<NoteState> &states)
{
    int first = std::numeric_limits<int>::max(), last = -1;
    for (const auto &state : states)
    {
        first = std::min(first, state.note->getStartFrame());
        last = std::max(last, state.note->getEndFrame());
    }
    project.markNoteEditDirtyRange(first, last);
}

void putBack(const NoteState &state)
{
    state.params.applyToNote(*state.note);
    state.note->setLastNonMacroMidiNote(state.lastNonMacroMidiNote);
    state.note->setBakedDeltaPitch(state.bakedDeltaPitch);
    state.note->markDirty();
    state.note->markSynthDirty();
}
} // namespace

void restore(Project &project, const std::vector<NoteState> &states)
{
    for (const auto &state : states)
        putBack(state);
    PitchCurveProcessor::rebuildBaseFromNotes(project);
    markChanged(project, states);
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

int apply(Project &project, const std::vector<NoteState> &original,
          const Settings &settings)
{
    for (const auto &state : original)
        putBack(state);

    const auto &reference = project.getReference();
    struct Matched
    {
        Note *note;
        float referencePitch;
    };
    std::vector<Matched> matched;
    if (reference.isAligned())
    {
        for (const auto &state : original)
        {
            auto *note = state.note;
            if (note->isRest())
                continue;
            const auto target = referencePitchOver(
                reference, note->getStartFrame(), note->getEndFrame(), settings.transpose);
            if (!target)
                continue;
            matched.push_back({note, *target});
            note->setMidiNote(state.params.midiNote +
                              settings.notePitch * (*target - state.params.midiNote));
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
    markChanged(project, original);
    return static_cast<int>(matched.size());
}
} // namespace ReferenceMatch
