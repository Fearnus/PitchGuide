#include "LiveProcessor.h"

#include "LiveEditor.h"
#include "../Utils/OnnxRuntime.h"
#include "../Utils/OnnxRuntimeLoader.h"

#include <cmath>

namespace
{
const juce::String kTransposeParamId = "transpose";
const juce::String kStateTag = "PitchGuideLive";
constexpr int kStateVersion = 1;
} // namespace

PitchGuideLiveProcessor::PitchGuideLiveProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Exactly as PitchGuide's plug-in does. Referencing the loader also links
    // its delay-load hook into this binary; without it Windows resolves
    // onnxruntime.dll from System32 (an older ORT that cannot serve our API
    // version) and the first ONNX call dereferences a null API table.
    OnnxRuntimeLoader::ensureLoadedFromLocalDirectory();
    juce::String onnxRuntimeError;
    if (!OnnxRuntime::initialise(&onnxRuntimeError))
    {
        DBG("PitchGuide Live: failed to initialise ONNX Runtime: " + onnxRuntimeError);
        jassertfalse;
    }

    transposeParameter = parameters.getRawParameterValue(kTransposeParamId);
    updateSavedState();
    startTimerHz(30);
}

PitchGuideLiveProcessor::~PitchGuideLiveProcessor()
{
    stopTimer();
    alive->store(false);
    analysisJob.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout
PitchGuideLiveProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{kTransposeParamId, 1}, "Transpose", -kMaxTranspose, kMaxTranspose, 0,
        juce::AudioParameterIntAttributes().withLabel("st")));
    return layout;
}

bool PitchGuideLiveProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    auto monoOrStereo = [](const juce::AudioChannelSet &set) {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };
    return monoOrStereo(in) && monoOrStereo(out) && out.size() >= in.size();
}

void PitchGuideLiveProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    detector.prepare(sampleRate);
    detector.setGateDb(gateDb.load());
    monoScratch.setSize(1, std::max(512, samplesPerBlock), false, false, true);
    transportSampleRate.store(sampleRate);
    setLatencySamples(0);
}

void PitchGuideLiveProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int inputs = getTotalNumInputChannels();
    const int outputs = getTotalNumOutputChannels();

    // The voice passes through untouched; a mono input feeds every output.
    for (int channel = inputs; channel < outputs; ++channel)
    {
        if (inputs > 0)
            buffer.copyFrom(channel, 0, buffer, 0, 0, numSamples);
        else
            buffer.clear(channel, 0, numSamples);
    }

    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    double hostSeconds = 0.0;
    bool playing = false;
    if (auto *hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            playing = position->getIsPlaying();
            if (auto samples = position->getTimeInSamples())
                hostSeconds = static_cast<double>(*samples) / sampleRate;
            else if (auto seconds = position->getTimeInSeconds())
                hostSeconds = *seconds;
        }
    }
    transportSeconds.store(hostSeconds);
    transportPlaying.store(playing);
    transportWallMs.store(juce::Time::getMillisecondCounterHiRes());

    if (inputs <= 0 || numSamples <= 0)
        return;

    detector.setGateDb(gateDb.load());
    auto pushFrame = [this](const LivePitchFrame &frame) {
        const auto scope = fifo.write(1);
        if (scope.blockSize1 > 0)
            fifoFrames[static_cast<size_t>(scope.startIndex1)] = frame;
        else if (scope.blockSize2 > 0)
            fifoFrames[static_cast<size_t>(scope.startIndex2)] = frame;
        // A full FIFO (editor and timer both stalled) drops the frame.
    };

    float *mono = monoScratch.getWritePointer(0);
    const int chunkSize = monoScratch.getNumSamples();
    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int chunk = std::min(chunkSize, numSamples - offset);
        juce::FloatVectorOperations::copy(mono, buffer.getReadPointer(0, offset), chunk);
        for (int channel = 1; channel < inputs; ++channel)
            juce::FloatVectorOperations::add(mono, buffer.getReadPointer(channel, offset), chunk);
        if (inputs > 1)
            juce::FloatVectorOperations::multiply(mono, 1.0f / static_cast<float>(inputs), chunk);
        detector.process(mono, chunk, hostSeconds + offset / sampleRate, playing, pushFrame);
    }
}

void PitchGuideLiveProcessor::drainLiveFrames()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return;

    const double latencySeconds = options.latencyMs / 1000.0;
    auto take = [this, latencySeconds](int start, int count) {
        for (int i = 0; i < count; ++i)
        {
            const auto &frame = fifoFrames[static_cast<size_t>(start + i)];
            trail.add(frame, latencySeconds);
            recent[static_cast<size_t>(recentHead)] = frame;
            recentHead = (recentHead + 1) % kRecentFrames;
            recentCount = std::min(recentCount + 1, kRecentFrames);
            latestFrame = frame;
        }
    };
    {
        const auto scope = fifo.read(ready);
        take(scope.startIndex1, scope.blockSize1);
        take(scope.startIndex2, scope.blockSize2);
    }
    latestFrameWallMs = juce::Time::getMillisecondCounterHiRes();
}

double PitchGuideLiveProcessor::estimateHostSeconds() const
{
    double seconds = transportSeconds.load();
    if (transportPlaying.load())
    {
        const double elapsed =
            (juce::Time::getMillisecondCounterHiRes() - transportWallMs.load()) / 1000.0;
        // Never run more than a quarter second past the last block: if the
        // host stops calling, the view stops too.
        seconds += juce::jlimit(0.0, 0.25, elapsed);
    }
    return seconds;
}

int PitchGuideLiveProcessor::getTranspose() const
{
    return transposeParameter != nullptr
               ? static_cast<int>(std::lround(transposeParameter->load()))
               : 0;
}

void PitchGuideLiveProcessor::setTranspose(int semitones)
{
    semitones = juce::jlimit(-kMaxTranspose, kMaxTranspose, semitones);
    if (auto *parameter = parameters.getParameter(kTransposeParamId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(
            parameter->convertTo0to1(static_cast<float>(semitones)));
        parameter->endChangeGesture();
    }
}

void PitchGuideLiveProcessor::setOptions(const Options &newOptions)
{
    const bool lyricsMoved = newOptions.lyricsOffsetSeconds != options.lyricsOffsetSeconds;
    options = newOptions;
    options.lyricsOffsetSeconds = juce::jlimit(-120.0, 120.0, options.lyricsOffsetSeconds);
    options.toleranceCents = juce::jlimit(5.0f, 200.0f, options.toleranceCents);
    options.latencyMs = juce::jlimit(0.0f, 1000.0f, options.latencyMs);
    options.gateDb = juce::jlimit(-90.0f, 0.0f, options.gateDb);
    gateDb.store(options.gateDb);
    if (lyricsMoved)
        retimeLyrics();
    updateSavedState();
}

void PitchGuideLiveProcessor::loadReference(const juce::File &file, bool syncLyrics)
{
    syncLyricsWhenAnalysed = syncLyrics;
    lastError.clear();
    analysisProgress = 0.0;
    analysisMessage = "Analysing " + file.getFileName();
    analysingFile = file;
    analysisJob.reset();
    analysisJob = std::make_unique<ReferenceNotesJob>(
        file,
        [this](double progress, const juce::String &message) {
            analysisProgress = progress;
            if (message.isNotEmpty())
                analysisMessage = message;
        },
        [this](std::optional<KaraokeReference> result, const juce::String &error) {
            analysisJob.reset();
            analysingFile = juce::File();
            if (result)
                applyReference(std::move(*result), syncLyricsWhenAnalysed);
            else
                lastError = error;
            updateSavedState();
        });
    updateSavedState();
}

void PitchGuideLiveProcessor::clearReference()
{
    analysisJob.reset();
    analysingFile = juce::File();
    reference = KaraokeReference();
    lastError.clear();
    retimeLyrics();
    updateSavedState();
}

void PitchGuideLiveProcessor::applyReference(KaraokeReference newReference, bool syncLyrics)
{
    reference = std::move(newReference);
    lastError.clear();
    // The old attempt was sung against another song.
    trail.clear();
    if (!syncLyrics || !autoSyncLyrics())
        retimeLyrics();
    updateSavedState();
}

void PitchGuideLiveProcessor::setLyricsText(const juce::String &text)
{
    lyricsText = text.trim();
    lyrics = KaraokeLyrics::parse(lyricsText);
    options.lyricsOffsetSeconds = 0.0;
    // New lyrics get lined up with the reference as soon as there is one.
    syncLyricsWhenAnalysed = true;
    if (!autoSyncLyrics())
        retimeLyrics();
    updateSavedState();
}

bool PitchGuideLiveProcessor::loadLyricsFile(const juce::File &file)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024)
        return false;
    setLyricsText(file.loadFileAsString());
    return true;
}

bool PitchGuideLiveProcessor::autoSyncLyrics()
{
    if (lyrics.isEmpty() || lyrics.sourceTiming == KaraokeLyrics::SourceTiming::None ||
        !reference.isLoaded())
        return false;
    // Only move lyrics that clearly fit better elsewhere: most LRC files are
    // already right for the album version.
    const auto offset = lyrics.estimateOffset(reference, true);
    options.lyricsOffsetSeconds = offset.value_or(0.0);
    retimeLyrics();
    updateSavedState();
    return true;
}

void PitchGuideLiveProcessor::retimeLyrics()
{
    lyrics.applyTiming(reference.isLoaded() ? &reference : nullptr, options.lyricsOffsetSeconds);
    ++lyricsRevision;
}

void PitchGuideLiveProcessor::updateSavedState()
{
    const juce::ScopedLock lock(stateLock);
    savedOptions = options;
    savedLyricsText = lyricsText;
    if (reference.isLoaded())
    {
        savedReferencePath = reference.sourcePath;
        savedReferenceData = reference.toMemoryBlock().toBase64Encoding();
    }
    else
    {
        savedReferencePath.clear();
        savedReferenceData.clear();
    }
    // A reference still being analysed is saved by path and analysed again on load.
    if (analysingFile != juce::File())
    {
        savedReferencePath = analysingFile.getFullPathName();
        savedReferenceData.clear();
    }
}

void PitchGuideLiveProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    juce::XmlElement xml(kStateTag);
    xml.setAttribute("version", kStateVersion);
    {
        const juce::ScopedLock lock(stateLock);
        xml.setAttribute("referenceStart", savedOptions.referenceStartSeconds);
        xml.setAttribute("latencyMs", savedOptions.latencyMs);
        xml.setAttribute("toleranceCents", savedOptions.toleranceCents);
        xml.setAttribute("gateDb", savedOptions.gateDb);
        xml.setAttribute("pixelsPerSecond", savedOptions.pixelsPerSecond);
        xml.setAttribute("editorWidth", savedOptions.editorWidth);
        xml.setAttribute("editorHeight", savedOptions.editorHeight);
        xml.setAttribute("lyricsOffset", savedOptions.lyricsOffsetSeconds);
        if (savedReferencePath.isNotEmpty())
        {
            auto *element = xml.createNewChildElement("Reference");
            element->setAttribute("path", savedReferencePath);
            if (savedReferenceData.isNotEmpty())
                element->addTextElement(savedReferenceData);
        }
        if (savedLyricsText.isNotEmpty())
            xml.createNewChildElement("Lyrics")->addTextElement(savedLyricsText);
    }
    if (auto parameterXml = parameters.copyState().createXml())
        xml.addChildElement(parameterXml.release());
    copyXmlToBinary(xml, destData);
}

void PitchGuideLiveProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::shared_ptr<juce::XmlElement> xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(kStateTag))
        return;

    if (auto *parameterXml = xml->getChildByName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*parameterXml));

    auto apply = [this, xml]() {
        Options restored = options;
        restored.referenceStartSeconds =
            xml->getDoubleAttribute("referenceStart", restored.referenceStartSeconds);
        restored.latencyMs =
            static_cast<float>(xml->getDoubleAttribute("latencyMs", restored.latencyMs));
        restored.toleranceCents = static_cast<float>(
            xml->getDoubleAttribute("toleranceCents", restored.toleranceCents));
        restored.gateDb = static_cast<float>(xml->getDoubleAttribute("gateDb", restored.gateDb));
        restored.pixelsPerSecond = static_cast<float>(
            xml->getDoubleAttribute("pixelsPerSecond", restored.pixelsPerSecond));
        restored.editorWidth = xml->getIntAttribute("editorWidth", restored.editorWidth);
        restored.editorHeight = xml->getIntAttribute("editorHeight", restored.editorHeight);
        restored.lyricsOffsetSeconds = xml->getDoubleAttribute("lyricsOffset", 0.0);

        // Saved lyrics come back with their saved offset, not synced again.
        auto *lyricsElement = xml->getChildByName("Lyrics");
        lyricsText = lyricsElement != nullptr ? lyricsElement->getAllSubText().trim()
                                              : juce::String();
        lyrics = KaraokeLyrics::parse(lyricsText);
        syncLyricsWhenAnalysed = false;
        setOptions(restored);
        retimeLyrics();

        if (auto *element = xml->getChildByName("Reference"))
        {
            const auto path = element->getStringAttribute("path");
            juce::MemoryBlock block;
            const auto text = element->getAllSubText().trim();
            std::optional<KaraokeReference> restoredReference;
            if (text.isNotEmpty() && block.fromBase64Encoding(text))
                restoredReference = KaraokeReference::fromMemoryBlock(block);
            if (restoredReference && restoredReference->isLoaded())
                applyReference(std::move(*restoredReference), false);
            else if (juce::File::isAbsolutePath(path) && juce::File(path).existsAsFile())
                loadReference(juce::File(path), false);
        }
    };

    if (juce::MessageManager::existsAndIsCurrentThread())
        apply();
    else
        juce::MessageManager::callAsync([apply, alive = alive]() {
            if (alive->load())
                apply();
        });
}

juce::AudioProcessorEditor *PitchGuideLiveProcessor::createEditor()
{
    return new PitchGuideLiveEditor(*this);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new PitchGuideLiveProcessor();
}
