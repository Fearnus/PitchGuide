#include "../Source/Models/ProjectRegionMerge.h"
#include "../Source/Models/ProjectSerializer.h"
#include "TestAssert.h"
#include <iostream>

int main() {
  Project first, second;
  for (auto *p : {&first, &second}) {
    auto &a = p->getAudioData();
    a.sampleRate = SAMPLE_RATE;
    a.waveform.setSize(1, 30 * HOP_SIZE);
    a.waveform.clear();
    a.originalWaveform.makeCopyOf(a.waveform);
    a.f0.assign(30, p == &first ? 220.0f : 440.0f);
    a.rawF0 = a.f0;
    a.voicedMask.assign(30, p == &first);
    a.melSpectrogram.assign(30, std::vector<float>(NUM_MELS, p == &first ? 1.0f : 2.0f));
    for (int i = 0; i < a.waveform.getNumSamples(); ++i)
      a.waveform.setSample(0, i, p == &first ? 0.25f : 0.75f);
  }
  Note left(10, 20, 65), right(20, 30, 72);
  left.setLyric("left"); right.setLyric("right");
  right.setPhoneme("r"); right.setVolumeDb(-4);
  right.setPitchOffset(3); right.setVibrato(0.4f);
  right.setBakedDeltaPitch(std::vector<float>(10, 0.8f));
  left.setRenderedEdit(true); right.setRenderedEdit(true);
  first.addNote(left); second.addNote(right);
  // Stale notes outside a region must not leak into the merge.
  first.addNote(Note(20, 30, 40)); second.addNote(Note(10, 20, 40));
  second.setGlobalPitchOffset(2); second.setVolume(-2);
  const double start = (10.0 * HOP_SIZE + 0.25) / SAMPLE_RATE;
  const double cut = (20.0 * HOP_SIZE + 0.25) / SAMPLE_RATE;
  const double end = (30.0 * HOP_SIZE + 0.25) / SAMPLE_RATE;
  auto merged = mergeProjectPlaybackParts({{&first, start, cut}, {&second, cut, end}}, start, end);
  CHECK(merged && merged->getNotes().size() == 2);
  const auto *note = merged->getNoteAtFrame(25);
  CHECK(note && note->getLyric() == "right" && note->getPhoneme() == "r");
  CHECK(note->getMidiNote() == 72 && note->getPitchOffset() == 5);
  CHECK(note->getVolumeDb() == -6 && note->getVibrato() == 0.4f);
  CHECK(note->getBakedDeltaPitch().front() == 0.8f && note->hasRenderedEdit());
  const auto &a = merged->getAudioData();
  CHECK(a.waveform.getSample(0, 15 * HOP_SIZE) == 0.25f);
  CHECK(a.waveform.getSample(0, 25 * HOP_SIZE) == 0.75f);
  CHECK(a.f0[15] == 220 && a.f0[25] == 440);
  CHECK(a.voicedMask[15] && !a.voicedMask[25]);
  CHECK(a.melSpectrogram[25][0] == 2);
  CHECK(a.playbackRegionRanges.size() == 1 && a.playbackRegionRanges[0].second == end);
  CHECK(second.getNoteAtFrame(25)->getPitchOffset() == 3);
  CHECK(!mergeProjectPlaybackParts({{&first, start, cut}, {&second, cut + 1, end}}, start, end));
  CHECK(!mergeProjectPlaybackParts({{&first, start, cut}, {&second, cut - 0.1, end}}, start, end));
  CHECK(!mergeProjectPlaybackParts({{&first, start, end}}, start, end));
  juce::MemoryBlock archive;
  CHECK(ProjectSerializer::toBinaryArchive(*merged, archive,
      ProjectSerializer::BinaryArchiveMode::hostBackedARA));
  Project restored;
  CHECK(ProjectSerializer::fromBinaryArchive(restored, archive.getData(), archive.getSize()));
  CHECK(restored.getNotes().size() == 2);
  CHECK(restored.getNoteAtFrame(25)->getLyric() == "right");
  CHECK(restored.getNoteAtFrame(25)->getPitchOffset() == 5);
  CHECK(restored.getAudioData().f0[25] == 440);
  Project splitAgain = *merged;
  clipProjectToPlaybackRange(splitAgain, cut, end);
  CHECK(splitAgain.getNotes().size() == 1);
  CHECK(splitAgain.getNoteAtFrame(25)->getBakedDeltaPitch().front() == 0.8f);
  // Source-less restored projects retain their notes and pitch data and signal hydration.
  second.getAudioData().waveform.setSize(0, 0);
  second.getAudioData().melSpectrogram.clear();
  merged = mergeProjectPlaybackParts({{&first, start, cut}, {&second, cut, end}}, start, end);
  CHECK(merged && merged->getNotes().size() == 2);
  CHECK(merged->getAudioData().waveform.getNumSamples() == 0);
  CHECK(merged->getAudioData().melSpectrogram.empty());
  CHECK(merged->getAudioData().f0[25] == 440);
  std::cout << "ARA region merge tests passed\n";
}
