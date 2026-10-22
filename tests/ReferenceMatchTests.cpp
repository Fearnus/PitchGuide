#include "../Source/Models/Project.h"
#include "../Source/Models/ProjectSerializer.h"
#include "../Source/Undo/ReferenceMatchAction.h"
#include "../Source/Utils/Constants.h"
#include "../Source/Utils/PitchCurveProcessor.h"
#include "../Source/Utils/ReferenceMatch.h"
#include "../Source/Utils/TakeTimeline.h"
#include "TestAssert.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

// A take with three notes and a reference aligned frame for frame, or
// `referenceDelay` frames later. The reference sings notes 1 and 2 at other
// pitches, with its own vibrato, and is silent over note 3. Each note is a
// region of its own, with 2 frames of unpitched audio on either side.
namespace {

constexpr int kFrames = 300;
constexpr double kTwoPi = 6.283185307179586;

struct Span {
    int start, end;
    float takeMidi, referenceMidi; // referenceMidi 0 = reference silent
};
constexpr Span kNotes[] = {{20, 90, 60.0f, 61.3f}, {110, 180, 62.0f, 66.8f}, {200, 270, 64.0f, 0.0f}};

// The reference's vibrato: 0.4 semitones at ~5.4 Hz.
float referenceVibrato(int frame) {
    return 0.4f * static_cast<float>(std::sin(kTwoPi * frame / 16.0));
}

Project makeProject(int referenceDelay = 0) {
    Project project;
    auto& audio = project.getAudioData();
    audio.sampleRate = SAMPLE_RATE;
    audio.melSpectrogram.assign(kFrames, std::vector<float>(NUM_MELS, -5.0f));
    audio.f0.assign(kFrames, 0.0f);
    audio.rawF0.assign(kFrames, 0.0f);
    audio.voicedMask.assign(kFrames, false);

    auto& reference = project.getReference();
    reference.sourcePath = "/songs/reference.wav";
    reference.f0.assign(static_cast<size_t>(kFrames + referenceDelay), 0.0f);
    reference.referenceFrameForTakeFrame.resize(kFrames);
    for (int frame = 0; frame < kFrames; ++frame)
        reference.referenceFrameForTakeFrame[static_cast<size_t>(frame)] =
            static_cast<float>(frame + referenceDelay);

    for (const auto& span : kNotes) {
        Note note(span.start, span.end, span.takeMidi);
        // The take's own contour: a slow 0.2 semitone wobble.
        std::vector<float> contour(static_cast<size_t>(span.end - span.start));
        for (size_t i = 0; i < contour.size(); ++i)
            contour[i] = 0.2f * static_cast<float>(std::sin(kTwoPi * static_cast<double>(i) / 40.0));
        note.setOriginalDeltaPitch(contour);
        project.addNote(std::move(note));
        audio.segmentChunkRanges.emplace_back(span.start - 2, span.end + 2);
        for (int frame = span.start; frame < span.end; ++frame) {
            audio.f0[static_cast<size_t>(frame)] = midiToFreq(span.takeMidi);
            audio.rawF0[static_cast<size_t>(frame)] = midiToFreq(span.takeMidi);
            audio.voicedMask[static_cast<size_t>(frame)] = true;
            if (span.referenceMidi > 0.0f)
                reference.f0[static_cast<size_t>(frame + referenceDelay)] =
                    midiToFreq(span.referenceMidi + referenceVibrato(frame));
        }
    }
    PitchCurveProcessor::rebuildBaseFromNotes(project);
    return project;
}

std::vector<Note*> allNotes(Project& project) {
    std::vector<Note*> notes;
    for (auto& note : project.getNotes())
        notes.push_back(&note);
    return notes;
}

float curveAt(const Project& project, int frame) {
    const auto& audio = project.getAudioData();
    return audio.basePitch[static_cast<size_t>(frame)] + audio.deltaPitch[static_cast<size_t>(frame)];
}

void checkNear(float actual, float expected, float tolerance, const char* what) {
    if (std::abs(actual - expected) > tolerance) {
        std::fprintf(stderr, "%s: %.3f, expected %.3f\n", what, actual, expected);
        CHECK(false);
    }
}

void movesNotesToTheReference() {
    auto project = makeProject();
    const auto original = ReferenceMatch::capture(allNotes(project));
    CHECK(ReferenceMatch::apply(project, original, {1.0f, 0.0f, 0.0f}).matched == 2);
    const auto& notes = project.getNotes();
    // The median ignores the vibrato.
    checkNear(notes[0].getMidiNote(), 61.3f, 0.05f, "note 1");
    checkNear(notes[1].getMidiNote(), 66.8f, 0.05f, "note 2");
    // The reference is silent over note 3.
    CHECK(notes[2].getMidiNote() == 64.0f);
    // Matching is a regular edit: the next Pitch Center pass starts from it.
    CHECK(notes[0].getLastNonMacroMidiNote() == notes[0].getMidiNote());

    ReferenceMatch::apply(project, original, {0.5f, 0.0f, 0.0f});
    checkNear(notes[0].getMidiNote(), 60.65f, 0.05f, "note 1 at 50%");
    checkNear(notes[1].getMidiNote(), 64.4f, 0.05f, "note 2 at 50%");

    ReferenceMatch::apply(project, original, {1.0f, 0.0f, -12.0f});
    checkNear(notes[0].getMidiNote(), 49.3f, 0.05f, "note 1 an octave down");
}

void copiesTheReferenceExpression() {
    auto project = makeProject();
    const auto original = ReferenceMatch::capture(allNotes(project));
    ReferenceMatch::apply(project, original, {1.0f, 1.0f, 0.0f});
    // Inside the notes the take now sings exactly what the reference sings.
    for (int frame = kNotes[1].start + 2; frame < kNotes[1].end - 2; ++frame)
        checkNear(curveAt(project, frame), 66.8f + referenceVibrato(frame), 0.02f, "curve");
    // Note 3 keeps its own contour.
    const auto untouched = makeProject();
    for (int frame = kNotes[2].start + 2; frame < kNotes[2].end - 2; ++frame)
        checkNear(curveAt(project, frame), curveAt(untouched, frame), 1e-4f, "note 3");
}

void expressionWorksAroundTheTakesOwnPitch() {
    auto project = makeProject();
    const auto original = ReferenceMatch::capture(allNotes(project));
    // Keep the take's pitch, take the reference's vibrato.
    ReferenceMatch::apply(project, original, {0.0f, 1.0f, 0.0f});
    const auto& notes = project.getNotes();
    CHECK(notes[0].getMidiNote() == 60.0f);
    // The take's own centre (60) with the reference's vibrato around it; the
    // tolerance covers the median of the sampled vibrato not being exactly 0.
    const int middle = (kNotes[0].start + kNotes[0].end) / 2;
    for (int frame = middle - 10; frame < middle + 10; ++frame)
        checkNear(curveAt(project, frame), 60.0f + referenceVibrato(frame), 0.06f,
                  "vibrato around the take's pitch");
}

void restoresExactly() {
    auto project = makeProject();
    const auto before = makeProject();
    const auto original = ReferenceMatch::capture(allNotes(project));
    ReferenceMatch::apply(project, original, {1.0f, 1.0f, 3.0f});

    auto check = [&before, &project]() {
        for (size_t i = 0; i < project.getNotes().size(); ++i) {
            const auto& note = project.getNotes()[i];
            const auto& expected = before.getNotes()[i];
            CHECK(note.getMidiNote() == expected.getMidiNote());
            CHECK(note.getLastNonMacroMidiNote() == expected.getLastNonMacroMidiNote());
            CHECK(note.getVibrato() == expected.getVibrato());
            CHECK(note.getBakedDeltaPitch() == expected.getBakedDeltaPitch());
        }
        for (int frame = 0; frame < kFrames; ++frame)
            checkNear(curveAt(project, frame), curveAt(before, frame), 1e-5f, "curve");
    };

    // Zero strength is the original...
    ReferenceMatch::apply(project, original, {0.0f, 0.0f, 0.0f});
    check();
    // ...and so is restoring the capture (undo).
    ReferenceMatch::apply(project, original, {1.0f, 1.0f, 3.0f});
    ReferenceMatch::restore(project, original);
    check();
}

void handlesNoReference() {
    auto project = makeProject();
    project.getReference() = {};
    const auto original = ReferenceMatch::capture(allNotes(project));
    CHECK(ReferenceMatch::apply(project, original, {1.0f, 1.0f, 0.0f}).matched == 0);
    CHECK(project.getNotes()[0].getMidiNote() == 60.0f);

    ReferenceTrack reference;
    CHECK(!ReferenceMatch::referencePitchOver(reference, 0, 10, 0.0f));
    CHECK(!ReferenceMatch::referencePitchOver(makeProject().getReference(), 10, 10, 0.0f));
}

// Matches the way the popup does: from the remembered bases, then confirms.
ReferenceMatch::Outcome matchAndConfirm(Project& project, const ReferenceMatch::Settings& settings) {
    const auto bases = ReferenceMatch::bases(project, allNotes(project));
    const auto outcome = ReferenceMatch::apply(project, bases, settings);
    ReferenceMatch::remember(project, bases, settings);
    return outcome;
}

// A fresh project matched once with `settings`.
Project matchedOnce(const ReferenceMatch::Settings& settings) {
    auto project = makeProject();
    ReferenceMatch::apply(project, ReferenceMatch::capture(allNotes(project)), settings);
    return project;
}

void checkSameMatch(const Project& actual, const Project& expected, const char* what) {
    for (size_t i = 0; i < actual.getNotes().size(); ++i) {
        const auto& note = actual.getNotes()[i];
        const auto& other = expected.getNotes()[i];
        checkNear(note.getMidiNote(), other.getMidiNote(), 1e-5f, what);
        CHECK(note.getStartFrame() == other.getStartFrame());
        CHECK(note.getEndFrame() == other.getEndFrame());
    }
    for (int frame = 0; frame < kFrames; ++frame)
        checkNear(curveAt(actual, frame), curveAt(expected, frame), 1e-5f, what);
}

// Reopening the popup picks up the settings last confirmed, and new settings
// apply to the notes as they were before matching instead of on top.
void reopeningAdjustsTheEarlierMatch() {
    auto project = makeProject();
    CHECK(matchAndConfirm(project, {0.6f, 0.5f, 2.0f}).changed == 2);
    const auto& memory = project.getReferenceMatch();
    CHECK(memory.settings.notePitch == 0.6f);
    CHECK(memory.settings.expression == 0.5f);
    CHECK(memory.settings.transpose == 2.0f);
    CHECK(memory.entries.size() == 2); // the reference is silent over note 3
    checkSameMatch(project, matchedOnce({0.6f, 0.5f, 2.0f}), "first match");

    // The same settings again change nothing, so nothing is re-rendered.
    CHECK(matchAndConfirm(project, {0.6f, 0.5f, 2.0f}).changed == 0);
    checkSameMatch(project, matchedOnce({0.6f, 0.5f, 2.0f}), "same settings");

    const ReferenceMatch::Settings adjustments[] = {
        {1.0f, 0.0f, 0.0f}, {0.6f, 0.2f, 2.0f}, {0.3f, 1.0f, -12.0f}};
    for (const auto& settings : adjustments) {
        matchAndConfirm(project, settings);
        checkSameMatch(project, matchedOnce(settings), "adjusted match");
    }

    // At zero the notes are back as sung, and nothing is left to remember
    // about them.
    matchAndConfirm(project, {0.0f, 0.0f, 0.0f});
    checkSameMatch(project, makeProject(), "match removed");
    CHECK(project.getReferenceMatch().entries.empty());
}

// A note edited after matching starts again from the edit; the others still
// start from before matching.
void editedNotesStartAgain() {
    auto project = makeProject();
    matchAndConfirm(project, {1.0f, 0.0f, 0.0f});
    auto& notes = project.getNotes();
    notes[0].setMidiNote(59.0f);
    PitchCurveProcessor::rebuildBaseFromNotes(project);

    const auto bases = ReferenceMatch::bases(project, allNotes(project));
    CHECK(bases[0].state.midiNote == 59.0f);
    CHECK(bases[1].state.midiNote == 62.0f);

    matchAndConfirm(project, {0.5f, 0.0f, 0.0f});
    checkNear(notes[0].getMidiNote(), 60.15f, 0.05f, "edited note at 50%");
    checkNear(notes[1].getMidiNote(), 64.4f, 0.05f, "note 2 at 50%");
}

// Undo and redo restore what is remembered along with the notes, so the next
// adjustment still starts from before the first match.
void undoRestoresWhatIsRemembered() {
    auto project = makeProject();
    matchAndConfirm(project, {0.5f, 0.0f, 0.0f});

    // A second match, recorded as the popup records it.
    const auto notes = allNotes(project);
    const auto opened = ReferenceMatch::capture(notes);
    const auto bases = ReferenceMatch::bases(project, notes);
    const ReferenceMatch::Settings settings{1.0f, 0.0f, 0.0f};
    ReferenceMatch::apply(project, bases, settings);
    const auto memoryBefore = project.getReferenceMatch();
    ReferenceMatch::remember(project, bases, settings);
    ReferenceMatchAction action(&project, opened, ReferenceMatch::capture(notes), memoryBefore,
                                project.getReferenceMatch());

    action.undo();
    checkNear(project.getNotes()[0].getMidiNote(), 60.65f, 0.05f, "undone to 50%");
    CHECK(project.getReferenceMatch().settings.notePitch == 0.5f);
    CHECK(ReferenceMatch::bases(project, notes)[0].state.midiNote == 60.0f);

    action.redo();
    checkNear(project.getNotes()[0].getMidiNote(), 61.3f, 0.05f, "redone to 100%");
    CHECK(project.getReferenceMatch().settings.notePitch == 1.0f);
    CHECK(ReferenceMatch::bases(project, notes)[0].state.midiNote == 60.0f);
}

// Plug-in state (binary archive) and .pitchnet documents (JSON) both keep what
// is remembered, closely enough that the reloaded notes still count as matched.
void survivesSaveAndLoad() {
    auto project = makeProject();
    matchAndConfirm(project, {0.7f, 0.4f, -1.0f});

    juce::MemoryBlock archive;
    CHECK(ProjectSerializer::toBinaryArchive(project, archive));
    Project fromArchive;
    CHECK(ProjectSerializer::fromBinaryArchive(fromArchive, archive.getData(), archive.getSize()));
    Project fromJson;
    CHECK(ProjectSerializer::fromJson(fromJson, ProjectSerializer::toJson(project, false, true)));

    for (auto* restored : {&fromArchive, &fromJson}) {
        // Loading rebuilds the mel spectrogram (the frame count) from the
        // take's audio, which this synthetic take does not have.
        restored->getAudioData().melSpectrogram = project.getAudioData().melSpectrogram;
        PitchCurveProcessor::rebuildBaseFromNotes(*restored);

        const auto memory = restored->getReferenceMatch();
        checkNear(memory.settings.notePitch, 0.7f, 1e-6f, "note pitch");
        checkNear(memory.settings.expression, 0.4f, 1e-6f, "expression");
        checkNear(memory.settings.transpose, -1.0f, 1e-6f, "transpose");
        CHECK(memory.entries.size() == 2);

        const auto bases = ReferenceMatch::bases(*restored, allNotes(*restored));
        checkNear(bases[0].state.midiNote, 60.0f, 1e-4f, "base after loading");
        CHECK(ReferenceMatch::apply(*restored, bases, memory.settings).changed == 0);
    }

    // Older projects, and projects never matched, open with the defaults.
    Project plain;
    juce::MemoryBlock plainArchive;
    CHECK(ProjectSerializer::toBinaryArchive(makeProject(), plainArchive));
    CHECK(ProjectSerializer::fromBinaryArchive(plain, plainArchive.getData(), plainArchive.getSize()));
    CHECK(plain.getReferenceMatch().settings.notePitch == 1.0f);
    CHECK(plain.getReferenceMatch().entries.empty());
}

// Timing moves the notes the reference sings over to where it sings them,
// carries the others along, and pitch matching still reads the reference
// where each note was sung.
void timingFollowsTheReference() {
    auto project = makeProject(10);
    const auto& notes = project.getNotes();
    for (float frame : {0.0f, 25.0f, 100.0f, 299.0f})
        checkNear(TakeTimeline(notes).sourceFrameAt(frame), frame, 1e-4f, "timeline untouched");

    const auto original = ReferenceMatch::capture(allNotes(project));
    CHECK(ReferenceMatch::apply(project, original, {0.0f, 0.0f, 0.0f, 1.0f}).changed == 3);
    CHECK(notes[0].getStartFrame() == 30 && notes[0].getEndFrame() == 100);
    CHECK(notes[1].getStartFrame() == 120 && notes[1].getEndFrame() == 190);
    // The reference is silent over note 3: it moves as much as note 2.
    CHECK(notes[2].getStartFrame() == 210 && notes[2].getEndFrame() == 280);
    CHECK(notes[0].getMidiNote() == 60.0f);

    ReferenceMatch::apply(project, original, {0.0f, 0.0f, 0.0f, 0.5f});
    CHECK(notes[0].getStartFrame() == 25 && notes[0].getEndFrame() == 95);

    // Pitch and expression: the note takes on the reference's vibrato where
    // timing put it.
    ReferenceMatch::apply(project, original, {1.0f, 1.0f, 0.0f, 1.0f});
    checkNear(notes[1].getMidiNote(), 66.8f, 0.05f, "note 2 after timing");
    for (int frame = 120 + 2; frame < 190 - 2; ++frame)
        checkNear(curveAt(project, frame), 66.8f + referenceVibrato(frame - 10), 0.02f,
                  "curve after timing");

    // The reference line follows the notes as sung.
    const TakeTimeline timeline(notes);
    checkNear(timeline.sourceFrameAt(130.0f), 120.0f, 1e-4f, "timeline inside a note");
    checkNear(timeline.sourceFrameAt(105.0f), 95.0f, 1e-4f, "timeline between notes");
    checkNear(timeline.sourceFrameAt(0.0f), -10.0f, 1e-4f, "timeline before the notes");

    ReferenceMatch::restore(project, original);
    checkSameMatch(project, makeProject(10), "timing restored");
}

// Timing never makes notes overlap: a note pushed toward a neighbour that is
// not being matched stops where the timing tool would stop it.
void timingRespectsNeighbours() {
    auto project = makeProject(40);
    const std::vector<Note*> firstOnly{&project.getNotes()[0]};
    ReferenceMatch::apply(project, ReferenceMatch::capture(firstOnly), {0.0f, 0.0f, 0.0f, 1.0f});
    const auto& notes = project.getNotes();
    // Note 1 would move to [60, 130). Note 2 stays at [110, 180), and their
    // regions need 2 + 2 frames of unpitched audio and a frame between them.
    CHECK(notes[0].getStartFrame() == 60);
    CHECK(notes[0].getEndFrame() == 105);
    CHECK(notes[1].getStartFrame() == 110 && notes[1].getEndFrame() == 180);
}

// Reopening adjusts timing from before the match too: notes are found by
// where they were sung, wherever timing moved them, also after saving.
void reopeningAdjustsTiming() {
    auto project = makeProject(10);
    matchAndConfirm(project, {1.0f, 0.5f, 0.0f, 1.0f});
    CHECK(project.getNotes()[0].getStartFrame() == 30);
    CHECK(ReferenceMatch::bases(project, allNotes(project))[0].state.startFrame == 20);
    CHECK(matchAndConfirm(project, {1.0f, 0.5f, 0.0f, 1.0f}).changed == 0);

    const ReferenceMatch::Settings settings{1.0f, 0.5f, 0.0f, 0.5f};
    matchAndConfirm(project, settings);
    auto once = makeProject(10);
    ReferenceMatch::apply(once, ReferenceMatch::capture(allNotes(once)), settings);
    checkSameMatch(project, once, "timing adjusted");

    juce::MemoryBlock archive;
    CHECK(ProjectSerializer::toBinaryArchive(project, archive));
    Project fromArchive;
    CHECK(ProjectSerializer::fromBinaryArchive(fromArchive, archive.getData(), archive.getSize()));
    Project fromJson;
    CHECK(ProjectSerializer::fromJson(fromJson, ProjectSerializer::toJson(project, false, true)));
    for (auto* restored : {&fromArchive, &fromJson}) {
        restored->getAudioData().melSpectrogram = project.getAudioData().melSpectrogram;
        PitchCurveProcessor::rebuildBaseFromNotes(*restored);
        checkNear(restored->getReferenceMatch().settings.timing, 0.5f, 1e-6f, "timing setting");
        const auto bases = ReferenceMatch::bases(*restored, allNotes(*restored));
        CHECK(bases[0].state.startFrame == 20 && bases[0].state.endFrame == 90);
        CHECK(ReferenceMatch::apply(*restored, bases, settings).changed == 0);
    }
}

} // namespace

int main() {
    movesNotesToTheReference();
    copiesTheReferenceExpression();
    expressionWorksAroundTheTakesOwnPitch();
    restoresExactly();
    handlesNoReference();
    reopeningAdjustsTheEarlierMatch();
    editedNotesStartAgain();
    undoRestoresWhatIsRemembered();
    survivesSaveAndLoad();
    timingFollowsTheReference();
    timingRespectsNeighbours();
    reopeningAdjustsTiming();
    std::cout << "Reference match tests passed\n";
}
