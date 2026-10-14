#include "../Source/UI/PianoRollComponent.h"
#include "../Source/Undo/PitchUndoManager.h"
#include "TestAssert.h"
#include <iostream>

static juce::MouseEvent event(PianoRollComponent& roll, juce::Point<float> point) {
  return {juce::Desktop::getInstance().getMainMouseSource(), point,
      juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
      &roll, &roll, juce::Time::getCurrentTime(), point, juce::Time::getCurrentTime(), 1, true};
}

int main() {
  juce::ScopedJuceInitialiser_GUI gui;
  Project a, b;
  for (auto* project : {&a, &b}) {
    project->getAudioData().sampleRate = SAMPLE_RATE;
    project->getAudioData().f0.assign(50, 440.0f);
    project->getAudioData().basePitch.assign(50, 88.0f);
    project->getAudioData().deltaPitch.assign(50, 0.0f);
    project->addNote(Note(3, 20, 88));
  }
  PianoRollComponent roll;
  PitchUndoManager history;
  roll.setSize(800, 400);
  roll.setProject(&a);
  roll.setUndoManager(&history);
  roll.setTrackViewMode(true);
  roll.setScrollX(0); roll.setScrollY(0);
  auto regions = std::make_shared<std::vector<MainViewRegionPreview>>();
  MainViewRegionPreview first;
  first.active = true; first.sourceProject = &a; first.startSeconds = 0; first.endSeconds = 0.5;
  regions->push_back(first);
  MainViewRegionPreview second;
  second.project = std::make_shared<Project>(b);
  second.sourceProject = &b; second.startSeconds = 1; second.endSeconds = 1.5;
  second.projectHostOffsetSeconds = 1;
  regions->push_back(second);
  roll.setRegionPreviews(regions);
  bool bAlive = true;
  int aRenders = 0, bRenders = 0;
  roll.onPitchEditFinished = [&] { ++aRenders; };
  roll.getTrackEditTargets = [&] {
    return std::vector<TrackPitchMacroTarget>{
        {&a, {{0, 50}}, [] { return true; }, {}},
        {&b, {{0, 50}}, [&] { return bAlive; }, [&](bool render) { if (render) ++bRenders; }}};
  };
  const float h = roll.getPixelsPerSemitone();
  const float header = PianoRollComponent::headerHeight;
  // A newly focused region can be published before its analysis completes.
  // Its canvas then gains notes, but selection must also receive the new
  // source binding without recreating the component/editor.
  (*regions)[0].sourceProject = nullptr;
  roll.setRegionPreviews(regions);
  CHECK(roll.beginTrackGesture(event(roll, {31, header + 1})));
  roll.updateTrackGesture(event(roll, {220, header + 3*h}));
  roll.finishTrackGesture();
  CHECK(!a.getNotes()[0].isSelected());
  a.getNotes()[0].setSelected(false);
  b.getNotes()[0].setSelected(false);
  auto analyzedRegions = std::make_shared<std::vector<MainViewRegionPreview>>(*regions);
  (*analyzedRegions)[0].sourceProject = &a;
  roll.setRegionPreviews(analyzedRegions);
  CHECK(roll.beginTrackGesture(event(roll, {31, header + 1})));
  roll.updateTrackGesture(event(roll, {220, header + 3*h}));
  roll.finishTrackGesture();
  CHECK(a.getNotes()[0].isSelected() && b.getNotes()[0].isSelected());
  CHECK(second.project->getNotes()[0].isSelected());
  CHECK(!history.canUndo());
  const float x = PianoRollComponent::pianoKeysWidth + framesToSeconds(10) * roll.getPixelsPerSecond();
  CHECK(roll.beginTrackGesture(event(roll, {x, header + 1.5f*h})));
  roll.updateTrackGesture(event(roll, {x, header + 0.5f*h}));
  roll.finishTrackGesture();
  CHECK(a.getNotes()[0].getMidiNote() == 89 && b.getNotes()[0].getMidiNote() == 89);
  CHECK(history.getUndoName() == "Drag Track Notes");
  CHECK(aRenders == 1 && bRenders == 1);
  bAlive = false;
  history.undo();
  CHECK(a.getNotes()[0].getMidiNote() == 88 && b.getNotes()[0].getMidiNote() == 89);
  CHECK(bRenders == 1);
  history.redo();
  CHECK(a.getNotes()[0].getMidiNote() == 89);
  std::cout << "Track gesture tests passed\n";
}
