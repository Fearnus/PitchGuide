#pragma once

#include "../JuceHeader.h"

class Project;

/**
 * The corrected take as a WAV file the user drags into a DAW.
 *
 * A DAW that accepts a dropped file usually references it where it lies
 * instead of copying it (FL Studio does), so these files have to stay put:
 * they go to a folder of their own, a file is never overwritten with a
 * different render, and dragging the same render again reuses its file
 * instead of piling up copies.
 */
namespace TakeExport
{
/** What the drag handle can do right now. */
enum class State
{
    noTake,    // nothing loaded or captured yet
    rendering, // an edit is still being resynthesized
    ready
};

/**
 * What to call the take: the audio file it came from, else the project file,
 * else the project's own name, else "Take". The suffix an earlier drag-out
 * added is dropped, so re-editing an exported take does not stack suffixes.
 */
juce::String takeNameFor(const Project &project);

/** "<take> (PitchGuide).wav" for the first file, "<take> (PitchGuide 2).wav" for the second, ... */
juce::String fileNameFor(const juce::String &takeName, int copyNumber);

/**
 * The channels worth writing. A mono take is spread over every output channel
 * for playback; when all channels hold the same samples, one is enough.
 */
juce::AudioBuffer<float> channelsToWrite(const juce::AudioBuffer<float> &audio);

/** Encodes `audio` as a 24-bit PCM WAV. */
bool encodeWav(const juce::AudioBuffer<float> &audio, int sampleRate,
               juce::MemoryBlock &wav);

/**
 * Puts `wav` in `folder` under the first free name for `takeName`, or returns
 * the file there that already holds exactly these bytes. Returns an empty
 * File and sets `error` on failure.
 */
juce::File writeOrReuse(const juce::File &folder, const juce::String &takeName,
                        const juce::MemoryBlock &wav, juce::String &error);

/** The project's rendered take, written (or found) in `folder`. */
juce::File exportTake(const Project &project, const juce::File &folder,
                      juce::String &error);
} // namespace TakeExport
