#include "ReferenceMatch.h"

#include "../Undo/TimingAction.h"
#include "PitchCurveProcessor.h"
#include "TimingRegionUtils.h"

#include <algorithm>
#include <cmath>
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
            states.push_back({note, NoteMatchState::of(*note)});
    return states;
}

std::vector<NoteState> bases(const Project &project, const std::vector<Note *> &notes)
{
    auto states = capture(notes);
    const auto &memory = project.getReferenceMatch();
    for (auto &state : states)
        if (const auto *before = memory.stateBeforeMatching(*state.note))
            state.state = *before;
    return states;
}

namespace
{
std::vector<NoteMatchState> currentStates(const std::vector<NoteState> &states)
{
    std::vector<NoteMatchState> current;
    current.reserve(states.size());
    for (const auto &state : states)
        current.push_back(NoteMatchState::of(*state.note));
    return current;
}

// Marks the notes whose state is no longer `previous` for resynthesis, and
// returns how many changed by more than the saved precision: a project
// reloaded from JSON rematches only to within it, which is not worth a
// render or an undo step. Timing moves were marked by applyNoteTiming().
int markChanged(Project &project, const std::vector<NoteState> &states,
                const std::vector<NoteMatchState> &previous)
{
    int first = std::numeric_limits<int>::max(), last = -1, changed = 0;
    for (size_t i = 0; i < states.size(); ++i)
    {
        auto *note = states[i].note;
        const auto now = NoteMatchState::of(*note);
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

std::vector<NoteTimingState> timingOf(const std::vector<NoteState> &states)
{
    std::vector<NoteTimingState> timing;
    timing.reserve(states.size());
    for (const auto &state : states)
        timing.push_back({state.note, state.state.startFrame, state.state.endFrame});
    return timing;
}

// The song frame at which the reference sings take frame `frame` (as sung).
// Past the end of the take, the frames continue from the last one.
float songFrameFor(const ReferenceTrack &reference, int frame)
{
    const auto &map = reference.referenceFrameForTakeFrame;
    const int last = static_cast<int>(map.size()) - 1;
    if (frame <= last)
        return map[static_cast<size_t>(std::max(0, frame))];
    return map[static_cast<size_t>(last)] + static_cast<float>(frame - last);
}

// A note on the song timeline while timing is laid out.
struct Slot
{
    const Note *note = nullptr;
    int start = 0;
    int end = 0;
    bool movable = false;
    timingRegions::SourceRegion region;
};

// Frames needed between two consecutive notes: none between notes sung back
// to back, one where a rest was sung between them, and between two regions
// room for the end of one and the start of the next region's unpitched audio
// plus a frame (as the timing tool requires).
int gapBetween(const Slot &previous, const Slot &next)
{
    if (previous.region.index != next.region.index)
        return previous.region.end - previous.note->getSrcEndFrame() +
               next.note->getSrcStartFrame() - next.region.start + 1;
    return previous.note->getSrcEndFrame() < next.note->getSrcStartFrame() ? 1 : 0;
}

// Moves the movable slots as little as needed to satisfy the timing tool's
// rules. Returns false if they cannot be met.
bool fitTiming(std::vector<Slot> &slots, int frameCount)
{
    if (slots.empty())
        return true;
    const auto earliestStart = [&slots](size_t i) {
        return i == 0 ? slots[0].note->getSrcStartFrame() - slots[0].region.start
                      : slots[i - 1].end + gapBetween(slots[i - 1], slots[i]);
    };
    const auto latestEnd = [&slots, frameCount](size_t i) {
        return i + 1 == slots.size()
                   ? frameCount - (slots[i].region.end - slots[i].note->getSrcEndFrame())
                   : slots[i + 1].start - gapBetween(slots[i], slots[i + 1]);
    };

    for (size_t i = 0; i < slots.size(); ++i)
        if (auto &slot = slots[i]; slot.movable)
        {
            slot.start = std::max(slot.start, earliestStart(i));
            slot.end = std::max(slot.end, slot.start + 1);
        }
    for (size_t i = slots.size(); i-- > 0;)
        if (auto &slot = slots[i]; slot.movable)
        {
            slot.end = std::min(slot.end, latestEnd(i));
            slot.start = std::min(slot.start, slot.end - 1);
        }

    for (size_t i = 0; i < slots.size(); ++i)
    {
        const bool involved = slots[i].movable || (i > 0 && slots[i - 1].movable);
        if (!involved)
            continue;
        if (slots[i].end - slots[i].start < 1 || slots[i].start < earliestStart(i))
            return false;
        if (slots[i].movable && i + 1 == slots.size() && slots[i].end > latestEnd(i))
            return false;
    }
    return true;
}

// Where timing puts each base: `timing` of the way from its base frames to
// where the reference sings its source frames, for the notes the reference
// sings over. The other notes being matched move along with those: each edge
// shifts as much as the nearest sung edges around it, interpolated by where
// it was sung. fitTiming() then lays them out; notes not being matched stay.
std::vector<NoteTimingState> timingTargets(const Project &project,
                                           const std::vector<NoteState> &bases,
                                           const std::vector<bool> &sung, float timing)
{
    auto targets = timingOf(bases);
    const auto &reference = project.getReference();
    if (timing <= 0.0f || !reference.isAligned())
        return targets;

    std::vector<Slot> slots;
    for (auto &note : project.getNotes())
        if (!note.isRest())
            slots.push_back({&note, note.getStartFrame(), note.getEndFrame(), false,
                             timingRegions::getSourceRegion(project, note)});
    std::sort(slots.begin(), slots.end(), [](const Slot &a, const Slot &b) {
        return a.note->getSrcStartFrame() < b.note->getSrcStartFrame();
    });
    const auto slotOf = [&slots](const Note *note) {
        return std::find_if(slots.begin(), slots.end(),
                            [note](const Slot &slot) { return slot.note == note; });
    };

    // How far timing moves the sung edges, by source frame.
    struct Shift
    {
        int srcFrame;
        float frames;
    };
    const auto shiftOf = [&reference, timing](int srcFrame, int from) {
        return timing * (songFrameFor(reference, srcFrame) - static_cast<float>(from));
    };
    std::vector<Shift> shifts;
    for (size_t i = 0; i < bases.size(); ++i)
    {
        const auto &base = bases[i];
        if (!sung[i] || slotOf(base.note) == slots.end())
            continue;
        shifts.push_back({base.note->getSrcStartFrame(),
                          shiftOf(base.note->getSrcStartFrame(), base.state.startFrame)});
        shifts.push_back({base.note->getSrcEndFrame(),
                          shiftOf(base.note->getSrcEndFrame(), base.state.endFrame)});
    }
    if (shifts.empty())
        return targets;
    std::sort(shifts.begin(), shifts.end(),
              [](const Shift &a, const Shift &b) { return a.srcFrame < b.srcFrame; });
    const auto shiftAt = [&shifts](int srcFrame) {
        const auto next = std::lower_bound(shifts.begin(), shifts.end(), srcFrame,
                                           [](const Shift &shift, int frame) { return shift.srcFrame < frame; });
        if (next == shifts.end())
            return shifts.back().frames;
        if (next == shifts.begin() || next->srcFrame == srcFrame)
            return next->frames;
        const auto &previous = *(next - 1);
        const float t = static_cast<float>(srcFrame - previous.srcFrame) /
                        static_cast<float>(next->srcFrame - previous.srcFrame);
        return previous.frames + t * (next->frames - previous.frames);
    };

    for (size_t i = 0; i < bases.size(); ++i)
    {
        const auto &base = bases[i];
        const auto slot = slotOf(base.note);
        if (slot == slots.end())
            continue;
        const int srcStart = base.note->getSrcStartFrame();
        const int srcEnd = base.note->getSrcEndFrame();
        const float startShift =
            sung[i] ? shiftOf(srcStart, base.state.startFrame) : shiftAt(srcStart);
        const float endShift = sung[i] ? shiftOf(srcEnd, base.state.endFrame) : shiftAt(srcEnd);
        slot->movable = true;
        slot->start = base.state.startFrame + static_cast<int>(std::lround(startShift));
        slot->end = base.state.endFrame + static_cast<int>(std::lround(endShift));
    }
    if (!fitTiming(slots, project.getAudioData().getNumFrames()))
        return targets;

    for (auto &target : targets)
        if (const auto slot = slotOf(target.note); slot != slots.end())
        {
            target.startFrame = slot->start;
            target.endFrame = slot->end;
        }
    return targets;
}
} // namespace

int restore(Project &project, const std::vector<NoteState> &states)
{
    const auto previous = currentStates(states);
    applyNoteTiming(project, timingOf(states));
    for (const auto &state : states)
        state.state.applyPitchTo(*state.note);
    PitchCurveProcessor::rebuildBaseFromNotes(project);
    return markChanged(project, states, previous);
}

std::optional<float> referencePitchOver(const ReferenceTrack &reference,
                                        int srcStartFrame, int srcEndFrame,
                                        float transpose)
{
    std::vector<float> sung;
    for (int frame = srcStartFrame; frame < srcEndFrame; ++frame)
        if (const float midi = reference.targetMidiAt(frame); midi > 0.0f)
            sung.push_back(midi);
    const int length = srcEndFrame - srcStartFrame;
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
    const auto previous = currentStates(bases);
    const auto &reference = project.getReference();

    // The reference's pitch over each note, where it sings over it.
    std::vector<std::optional<float>> targets(bases.size());
    std::vector<bool> sung(bases.size(), false);
    if (reference.isAligned())
        for (size_t i = 0; i < bases.size(); ++i)
        {
            const auto *note = bases[i].note;
            if (note->isRest())
                continue;
            targets[i] = referencePitchOver(reference, note->getSrcStartFrame(),
                                            note->getSrcEndFrame(), settings.transpose);
            sung[i] = targets[i].has_value();
        }

    applyNoteTiming(project, timingTargets(project, bases, sung, settings.timing));
    for (size_t i = 0; i < bases.size(); ++i)
    {
        const auto &base = bases[i];
        base.state.applyPitchTo(*base.note);
        if (targets[i])
            base.note->setMidiNote(base.state.midiNote +
                                   settings.notePitch * (*targets[i] - base.state.midiNote));
    }
    PitchCurveProcessor::rebuildBaseFromNotes(project);

    if (settings.expression > 0.0f)
    {
        // Bake each note's new contour: its current curve, moved part of the
        // way to the reference's curve carried to the note's new centre. The
        // contour then already holds the result, so the note's contour tools
        // are reset (as a pitch drawing does) rather than applied on top.
        const auto &audioData = project.getAudioData();
        for (size_t n = 0; n < bases.size(); ++n)
        {
            if (!targets[n])
                continue;
            auto *note = bases[n].note;
            const int start = note->getStartFrame();
            const int length = std::max(0, note->getEndFrame() - start);
            const int srcStart = note->getSrcStartFrame();
            const int srcLength = std::max(0, note->getSrcDurationFrames());
            std::vector<float> baked(static_cast<size_t>(length), 0.0f);
            for (int i = 0; i < length; ++i)
            {
                const auto frame = static_cast<size_t>(start + i);
                if (frame >= audioData.basePitch.size() || frame >= audioData.deltaPitch.size())
                    break;
                const float base = audioData.basePitch[frame];
                float curve = base + audioData.deltaPitch[frame];
                // The take frame sung here, wherever timing put the note.
                const int srcFrame = srcStart + (2 * i + 1) * srcLength / (2 * length);
                if (const float sungMidi = reference.targetMidiAt(srcFrame); sungMidi > 0.0f)
                {
                    const float guide = note->getMidiNote() +
                                        (sungMidi + settings.transpose - *targets[n]);
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
    outcome.matched = static_cast<int>(std::count(sung.begin(), sung.end(), true));
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
        const auto existing = std::find_if(entries.begin(), entries.end(),
                                           [&note](const auto &entry) { return entry.isFor(note); });
        auto now = NoteMatchState::of(note);
        if (now.isCloseTo(base.state))
        {
            // Not matched (anymore): it starts from its current state anyway.
            if (existing != entries.end())
                entries.erase(existing);
            continue;
        }
        ReferenceMatchMemory::Entry entry{note.getSrcStartFrame(), note.getSrcEndFrame(),
                                          base.state, std::move(now)};
        if (existing != entries.end())
            *existing = std::move(entry);
        else
            entries.push_back(std::move(entry));
    }

    // Forget notes that were deleted, split or merged since they were matched.
    const auto &notes = project.getNotes();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&notes](const auto &entry) {
        return std::none_of(notes.begin(), notes.end(),
                            [&entry](const Note &note) { return entry.isFor(note); });
    }), entries.end());
}
} // namespace ReferenceMatch
