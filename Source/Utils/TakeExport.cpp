#include "TakeExport.h"

#include "../Models/Project.h"
#include "Localization.h"

#include <algorithm>

namespace TakeExport
{
namespace
{
const juce::String kSuffixTag = "PitchGuide";
constexpr int kMaxTakeNameLength = 80;
constexpr int kMaxCopies = 999;

// "Take (PitchGuide)" and "Take (PitchGuide 3)" -> "Take"; anything else as is.
juce::String withoutExportSuffix(const juce::String &name)
{
    const auto marker = " (" + kSuffixTag;
    const int at = name.lastIndexOf(marker);
    if (at < 0 || !name.endsWithChar(')'))
        return name;

    const auto copy = name.substring(at + marker.length(), name.length() - 1).trim();
    if (copy.isEmpty() || copy.containsOnly("0123456789"))
        return name.substring(0, at);
    return name;
}

juce::String stemOf(const juce::File &file)
{
    return file.getFullPathName().isEmpty() ? juce::String()
                                            : file.getFileNameWithoutExtension();
}
} // namespace

juce::String takeNameFor(const Project &project)
{
    juce::String name = stemOf(project.getFilePath());
    if (name.isEmpty())
        name = stemOf(project.getProjectFilePath());
    if (name.isEmpty() && project.getName() != "Untitled")
        name = project.getName();

    name = juce::File::createLegalFileName(withoutExportSuffix(name.trim()))
               .substring(0, kMaxTakeNameLength)
               .trim();
    return name.isEmpty() ? juce::String("Take") : name;
}

juce::String fileNameFor(const juce::String &takeName, int copyNumber)
{
    const auto copy = copyNumber > 1 ? " " + juce::String(copyNumber) : juce::String();
    return takeName + " (" + kSuffixTag + copy + ").wav";
}

juce::AudioBuffer<float> channelsToWrite(const juce::AudioBuffer<float> &audio)
{
    const int numSamples = audio.getNumSamples();
    bool identical = audio.getNumChannels() > 1;
    for (int channel = 1; identical && channel < audio.getNumChannels(); ++channel)
        identical = std::equal(audio.getReadPointer(0), audio.getReadPointer(0) + numSamples,
                               audio.getReadPointer(channel));

    juce::AudioBuffer<float> result(identical ? 1 : audio.getNumChannels(), numSamples);
    for (int channel = 0; channel < result.getNumChannels(); ++channel)
        result.copyFrom(channel, 0, audio, channel, 0, numSamples);
    return result;
}

bool encodeWav(const juce::AudioBuffer<float> &audio, int sampleRate,
               juce::MemoryBlock &wav)
{
    wav.reset();
    if (audio.getNumChannels() <= 0 || audio.getNumSamples() <= 0 || sampleRate <= 0)
        return false;

    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::MemoryOutputStream>(wav, false);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(sampleRate)
                    .withNumChannels(audio.getNumChannels())
                    .withBitsPerSample(24));
    if (writer == nullptr)
        return false;

    const bool written =
        writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    writer.reset(); // finishes the header and hands the bytes to `wav`
    return written && wav.getSize() > 0;
}

juce::File writeOrReuse(const juce::File &folder, const juce::String &takeName,
                        const juce::MemoryBlock &wav, juce::String &error)
{
    if (!folder.createDirectory())
    {
        error = TR("dialog.failed_open") + "\n" + folder.getFullPathName();
        return {};
    }

    for (int copy = 1; copy <= kMaxCopies; ++copy)
    {
        const auto file = folder.getChildFile(fileNameFor(takeName, copy));
        if (!file.exists())
        {
            // Written to a temporary file and renamed, so a DAW never picks up
            // half a take.
            if (file.replaceWithData(wav.getData(), wav.getSize()))
                return file;
            error = TR("dialog.failed_write") + "\n" + file.getFullPathName();
            return {};
        }

        juce::MemoryBlock existing;
        if (file.getSize() == static_cast<juce::int64>(wav.getSize()) &&
            file.loadFileAsData(existing) && existing == wav)
            return file;
    }

    error = TR("dialog.failed_open") + "\n" +
            folder.getChildFile(fileNameFor(takeName, kMaxCopies + 1)).getFullPathName();
    return {};
}

juce::File exportTake(const Project &project, const juce::File &folder,
                      juce::String &error)
{
    const auto &audio = project.getAudioData();
    juce::MemoryBlock wav;
    if (!encodeWav(channelsToWrite(audio.waveform), audio.sampleRate, wav))
    {
        error = TR("dialog.failed_create_writer") + "\n" +
                folder.getChildFile(fileNameFor(takeNameFor(project), 1)).getFullPathName();
        return {};
    }
    return writeOrReuse(folder, takeNameFor(project), wav, error);
}
} // namespace TakeExport
