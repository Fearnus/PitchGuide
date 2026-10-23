#include "../Source/Models/Project.h"
#include "../Source/Utils/TakeExport.h"
#include "TestAssert.h"

#include <cmath>
#include <iostream>

// The drag-out file: what it is called, what it holds, and that a file a DAW
// may already reference is never overwritten.
namespace {

constexpr int kRate = 44100;

juce::AudioBuffer<float> sine(int channels, int samples, float frequency = 220.0f) {
    juce::AudioBuffer<float> audio(channels, samples);
    for (int i = 0; i < samples; ++i) {
        const float value = 0.5f * std::sin(6.2831853f * frequency * static_cast<float>(i) /
                                            static_cast<float>(kRate));
        for (int channel = 0; channel < channels; ++channel)
            audio.setSample(channel, i, value);
    }
    return audio;
}

juce::MemoryBlock wavOf(const juce::AudioBuffer<float>& audio) {
    juce::MemoryBlock wav;
    CHECK(TakeExport::encodeWav(audio, kRate, wav));
    return wav;
}

// loadFileAsData() appends, so each read gets a block of its own.
juce::MemoryBlock contentsOf(const juce::File& file) {
    juce::MemoryBlock data;
    CHECK(file.loadFileAsData(data));
    return data;
}

int filesIn(const juce::File& folder) {
    return folder.getNumberOfChildFiles(juce::File::findFiles);
}

void namesTheTakeAfterItsSource() {
    Project project;
    CHECK(TakeExport::takeNameFor(project) == "Take");

    project.setName("Verse 2");
    CHECK(TakeExport::takeNameFor(project) == "Verse 2");

    project.setProjectFilePath(juce::File::getCurrentWorkingDirectory().getChildFile("song.pgproj"));
    CHECK(TakeExport::takeNameFor(project) == "song");

    project.setFilePath(juce::File::getCurrentWorkingDirectory().getChildFile("lead vocal take 3.wav"));
    CHECK(TakeExport::takeNameFor(project) == "lead vocal take 3");

    // An exported take that is edited again keeps its original name.
    project.setFilePath(juce::File::getCurrentWorkingDirectory().getChildFile("lead (PitchGuide).wav"));
    CHECK(TakeExport::takeNameFor(project) == "lead");
    project.setFilePath(juce::File::getCurrentWorkingDirectory().getChildFile("lead (PitchGuide 12).wav"));
    CHECK(TakeExport::takeNameFor(project) == "lead");
    project.setFilePath(juce::File::getCurrentWorkingDirectory().getChildFile("lead (PitchGuide mix).wav"));
    CHECK(TakeExport::takeNameFor(project) == "lead (PitchGuide mix)");

    // Characters a file name cannot hold are dropped.
    Project named;
    named.setName("Hook: take <2>?");
    CHECK(TakeExport::takeNameFor(named) == "Hook take 2");
}

void numbersCopies() {
    CHECK(TakeExport::fileNameFor("lead", 1) == "lead (PitchGuide).wav");
    CHECK(TakeExport::fileNameFor("lead", 2) == "lead (PitchGuide 2).wav");
    CHECK(TakeExport::fileNameFor("lead", 17) == "lead (PitchGuide 17).wav");
}

void writesOneChannelForAMonoTake() {
    CHECK(TakeExport::channelsToWrite(sine(2, 1000)).getNumChannels() == 1);
    CHECK(TakeExport::channelsToWrite(sine(1, 1000)).getNumChannels() == 1);

    auto stereo = sine(2, 1000);
    stereo.setSample(1, 500, 0.25f);
    const auto kept = TakeExport::channelsToWrite(stereo);
    CHECK(kept.getNumChannels() == 2);
    CHECK(kept.getSample(1, 500) == 0.25f);
    CHECK(kept.getSample(0, 500) == stereo.getSample(0, 500));
}

void encodesA24BitWav() {
    const auto audio = sine(1, 4410);
    const auto wav = wavOf(audio);

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(
        format.createReaderFor(new juce::MemoryInputStream(wav, false), true));
    CHECK(reader != nullptr);
    CHECK(reader->sampleRate == kRate);
    CHECK(reader->numChannels == 1);
    CHECK(reader->bitsPerSample == 24);
    CHECK(reader->lengthInSamples == audio.getNumSamples());

    juce::AudioBuffer<float> decoded(1, audio.getNumSamples());
    CHECK(reader->read(&decoded, 0, audio.getNumSamples(), 0, true, false));
    for (int i = 0; i < audio.getNumSamples(); ++i)
        CHECK(std::abs(decoded.getSample(0, i) - audio.getSample(0, i)) < 1.0e-6f);

    juce::MemoryBlock nothing;
    CHECK(!TakeExport::encodeWav(juce::AudioBuffer<float>(1, 0), kRate, nothing));
    CHECK(!TakeExport::encodeWav(audio, 0, nothing));
}

void neverOverwritesADifferentRender(const juce::File& folder) {
    const auto first = wavOf(sine(1, 2000, 220.0f));
    const auto second = wavOf(sine(1, 2000, 330.0f));
    juce::String error;

    const auto a = TakeExport::writeOrReuse(folder, "lead", first, error);
    CHECK(a.getFileName() == "lead (PitchGuide).wav");
    CHECK(contentsOf(a) == first);

    // Dragging the same render again reuses its file.
    CHECK(TakeExport::writeOrReuse(folder, "lead", first, error) == a);
    CHECK(filesIn(folder) == 1);

    // A new render gets a new file, and the first one stays as it was.
    const auto b = TakeExport::writeOrReuse(folder, "lead", second, error);
    CHECK(b.getFileName() == "lead (PitchGuide 2).wav");
    CHECK(contentsOf(a) == first);
    CHECK(contentsOf(b) == second);

    // Undoing back to the first render finds the first file again.
    CHECK(TakeExport::writeOrReuse(folder, "lead", first, error) == a);
    CHECK(TakeExport::writeOrReuse(folder, "lead", second, error) == b);
    CHECK(filesIn(folder) == 2);

    // A file the user put there under the same name is left alone too.
    const auto other = folder.getChildFile("bass (PitchGuide).wav");
    CHECK(other.replaceWithText("not a take"));
    CHECK(TakeExport::writeOrReuse(folder, "bass", first, error).getFileName() ==
          "bass (PitchGuide 2).wav");
    CHECK(other.loadFileAsString() == "not a take");
}

void exportsTheRenderedTake(const juce::File& folder) {
    Project project;
    juce::String error;
    CHECK(TakeExport::exportTake(project, folder, error) == juce::File());
    CHECK(error.isNotEmpty());

    // A mono take spread over a stereo output, as a dropped file is.
    auto& audio = project.getAudioData();
    audio.sampleRate = kRate;
    audio.waveform = sine(2, 3000);
    project.setFilePath(juce::File::getCurrentWorkingDirectory().getChildFile("chorus.wav"));

    error.clear();
    const auto file = TakeExport::exportTake(project, folder.getChildFile("nested"), error);
    CHECK(error.isEmpty());
    CHECK(file.getFileName() == "chorus (PitchGuide).wav");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    CHECK(reader != nullptr);
    CHECK(reader->numChannels == 1);
    CHECK(reader->lengthInSamples == 3000);
}

} // namespace

int main() {
    const auto folder = juce::File::createTempFile("pitchguide-take-export");
    CHECK(folder.createDirectory());

    namesTheTakeAfterItsSource();
    numbersCopies();
    writesOneChannelForAMonoTake();
    encodesA24BitWav();
    neverOverwritesADifferentRender(folder.getChildFile("overwrite"));
    exportsTheRenderedTake(folder.getChildFile("export"));

    folder.deleteRecursively();
    std::cout << "Take export tests passed\n";
}
