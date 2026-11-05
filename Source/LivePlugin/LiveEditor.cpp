#include "LiveEditor.h"

#include "../UI/Components/AppFont.h"
#include "../UI/Components/DarkLookAndFeel.h"
#include "../Utils/UI/Theme.h"
#include "LiveProcessor.h"

namespace
{
const juce::Colour kHeaderBackground(0xFF0F1116u);
const juce::Colour kHeaderBorder(0xFF262A34u);

/** Reference placement, latency, tolerance and the input gate. */
class LiveSettingsPanel : public juce::Component, private juce::Timer
{
public:
    explicit LiveSettingsPanel(PitchGuideLiveProcessor &p) : processor(p)
    {
        const auto &options = processor.getOptions();
        setupRow(startLabel, start, "Reference starts at", -30.0, 900.0, 0.01, " s",
                 options.referenceStartSeconds,
                 "Host time where the reference file begins. Put the playhead where the "
                 "song starts and press Use playhead.");
        setupRow(latencyLabel, latency, "Sync compensation", 0.0, 500.0, 1.0, " ms",
                 options.latencyMs,
                 "Moves your voice earlier on the roll by your interface's round-trip "
                 "latency, so a line sung on time lands on the note.");
        setupRow(toleranceLabel, tolerance, "In tune within", 10.0, 100.0, 5.0, " cents",
                 options.toleranceCents, "How far from the note still counts as in tune.");
        setupRow(gateLabel, gate, "Microphone gate", -80.0, -20.0, 1.0, " dB", options.gateDb,
                 "Quieter input is ignored. Raise it if room noise draws a line.");

        usePlayhead.onClick = [this]() {
            start.setValue(processor.estimateHostSeconds(), juce::sendNotificationSync);
        };
        addAndMakeVisible(usePlayhead);

        levelLabel.setFont(AppFont::getFont(12.5f));
        levelLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(levelLabel);

        setSize(470, 236);
        startTimerHz(15);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14, 12);
        auto layoutRow = [&area](juce::Label &label, juce::Slider &slider) {
            auto row = area.removeFromTop(30);
            label.setBounds(row.removeFromLeft(140));
            slider.setBounds(row);
            area.removeFromTop(8);
        };
        layoutRow(startLabel, start);
        layoutRow(latencyLabel, latency);
        layoutRow(toleranceLabel, tolerance);
        layoutRow(gateLabel, gate);
        auto bottom = area.removeFromTop(28);
        usePlayhead.setBounds(bottom.removeFromLeft(140));
        levelLabel.setBounds(bottom);
    }

private:
    void setupRow(juce::Label &label, juce::Slider &slider, const juce::String &name,
                  double minimum, double maximum, double step, const juce::String &suffix,
                  double value, const juce::String &tooltip)
    {
        label.setText(name, juce::dontSendNotification);
        label.setFont(AppFont::getFont(13.0f));
        label.setTooltip(tooltip);
        addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 88, 22);
        slider.setRange(minimum, maximum, step);
        slider.setTextValueSuffix(suffix);
        slider.setValue(value, juce::dontSendNotification);
        slider.setTooltip(tooltip);
        slider.onValueChange = [this]() { push(); };
        addAndMakeVisible(slider);
    }

    void push()
    {
        auto options = processor.getOptions();
        options.referenceStartSeconds = start.getValue();
        options.latencyMs = static_cast<float>(latency.getValue());
        options.toleranceCents = static_cast<float>(tolerance.getValue());
        options.gateDb = static_cast<float>(gate.getValue());
        processor.setOptions(options);
    }

    void timerCallback() override
    {
        const float level = processor.getLatestFrame().levelDb;
        const bool open = level >= processor.getOptions().gateDb;
        levelLabel.setText("Input " + (level > -100.0f ? juce::String(level, 0) + " dB" : "silent"),
                           juce::dontSendNotification);
        levelLabel.setColour(juce::Label::textColourId,
                             open ? juce::Colour(0xFF3DDC84u) : juce::Colours::white.withAlpha(0.5f));
    }

    PitchGuideLiveProcessor &processor;
    juce::Label startLabel, latencyLabel, toleranceLabel, gateLabel, levelLabel;
    juce::Slider start, latency, tolerance, gate;
    juce::TextButton usePlayhead{"Use playhead"};
};
} // namespace

PitchGuideLiveEditor::PitchGuideLiveEditor(PitchGuideLiveProcessor &p)
    : juce::AudioProcessorEditor(p), liveProcessor(p), view(p)
{
    AppFont::initialize();
    setLookAndFeel(&DarkLookAndFeel::getInstance());

    addAndMakeVisible(view);

    loadButton.onClick = [this]() { chooseReference(); };
    loadButton.setTooltip("Choose an isolated vocal of the song (wav, flac, mp3...). "
                          "You can also drop it on the window.");
    addAndMakeVisible(loadButton);

    referenceLabel.setFont(AppFont::getFont(13.0f));
    referenceLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));
    referenceLabel.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(referenceLabel);

    transposeCaption.setText("Transpose", juce::dontSendNotification);
    transposeCaption.setFont(AppFont::getFont(13.0f));
    transposeCaption.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    transposeCaption.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(transposeCaption);

    transposeValue.setFont(AppFont::getFont(15.0f));
    transposeValue.setJustificationType(juce::Justification::centred);
    transposeValue.setTooltip("Moves the reference into your range. Double-click to reset.");
    transposeValue.addMouseListener(this, false);
    addAndMakeVisible(transposeValue);

    auto transposeBy = [this](int semitones) {
        liveProcessor.setTranspose(liveProcessor.getTranspose() + semitones);
        updateHeader();
    };
    octaveDownButton.onClick = [transposeBy]() { transposeBy(-12); };
    semitoneDownButton.onClick = [transposeBy]() { transposeBy(-1); };
    semitoneUpButton.onClick = [transposeBy]() { transposeBy(1); };
    octaveUpButton.onClick = [transposeBy]() { transposeBy(12); };
    octaveDownButton.setTooltip("Reference an octave down");
    octaveUpButton.setTooltip("Reference an octave up");
    for (auto *button : {&octaveDownButton, &semitoneDownButton, &semitoneUpButton, &octaveUpButton})
        addAndMakeVisible(*button);

    clearButton.setTooltip("Forget what was sung so far and reset the score");
    clearButton.onClick = [this]() {
        liveProcessor.clearTrail();
        score = {};
        repaint();
    };
    addAndMakeVisible(clearButton);

    settingsButton.onClick = [this]() { showSettings(); };
    addAndMakeVisible(settingsButton);

    setResizable(true, true);
    setResizeLimits(640, 360, 3200, 1800);
    const auto &options = liveProcessor.getOptions();
    setSize(options.editorWidth, options.editorHeight);
    restoringSize = false;

    updateHeader();
    startTimerHz(60);
}

PitchGuideLiveEditor::~PitchGuideLiveEditor()
{
    stopTimer();
    transposeValue.removeMouseListener(this);
    setLookAndFeel(nullptr);
    AppFont::shutdown();
}

void PitchGuideLiveEditor::paint(juce::Graphics &g)
{
    auto header = getLocalBounds().removeFromTop(kHeaderHeight);
    g.setColour(kHeaderBackground);
    g.fillRect(header);
    g.setColour(kHeaderBorder);
    g.drawHorizontalLine(kHeaderHeight - 1, 0.0f, static_cast<float>(getWidth()));

    auto title = header.reduced(14, 0).removeFromLeft(140);
    g.setFont(AppFont::getFont(18.0f));
    g.setColour(juce::Colours::white);
    const juce::String brand = "PitchGuide";
    const int brandWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), brand);
    g.drawText(brand, title, juce::Justification::centredLeft);
    g.setColour(APP_COLOR_PRIMARY);
    g.drawText("Live", title.withTrimmedLeft(brandWidth + 5), juce::Justification::centredLeft);

    // Score, between transpose and Clear (see resized()).
    auto scoreArea = clearButton.getBounds().withX(clearButton.getX() - 122).withWidth(110);
    const bool hasScore = score.voicedSeconds >= 0.5;
    const float accuracy = score.accuracy();
    const auto colour = !hasScore             ? juce::Colours::white.withAlpha(0.45f)
                        : accuracy >= 0.75f   ? juce::Colour(0xFF3DDC84u)
                        : accuracy >= 0.5f    ? juce::Colour(0xFFFFD34Eu)
                                              : juce::Colour(0xFFFF5C5Cu);
    g.setColour(colour);
    g.setFont(AppFont::getFont(22.0f));
    auto number = scoreArea.removeFromLeft(58);
    g.drawText(hasScore ? juce::String(juce::roundToInt(accuracy * 100.0f)) + "%" : "--", number,
               juce::Justification::centredRight);
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(AppFont::getFont(11.5f));
    g.drawFittedText("in tune", scoreArea.withTrimmedLeft(6), juce::Justification::centredLeft, 1);
}

void PitchGuideLiveEditor::resized()
{
    auto bounds = getLocalBounds();
    view.setBounds(bounds.withTrimmedTop(kHeaderHeight));

    auto header = bounds.removeFromTop(kHeaderHeight).reduced(12, 10);
    header.removeFromLeft(140); // title
    loadButton.setBounds(header.removeFromLeft(132));
    header.removeFromLeft(10);

    settingsButton.setBounds(header.removeFromRight(84));
    header.removeFromRight(6);
    clearButton.setBounds(header.removeFromRight(60));
    header.removeFromRight(126); // score, painted
    octaveUpButton.setBounds(header.removeFromRight(38));
    header.removeFromRight(3);
    semitoneUpButton.setBounds(header.removeFromRight(30));
    transposeValue.setBounds(header.removeFromRight(58));
    semitoneDownButton.setBounds(header.removeFromRight(30));
    header.removeFromRight(3);
    octaveDownButton.setBounds(header.removeFromRight(38));
    transposeCaption.setBounds(header.removeFromRight(76));
    header.removeFromRight(8);
    referenceLabel.setBounds(header);

    if (!restoringSize)
    {
        auto options = liveProcessor.getOptions();
        if (options.editorWidth != getWidth() || options.editorHeight != getHeight())
        {
            options.editorWidth = getWidth();
            options.editorHeight = getHeight();
            liveProcessor.setOptions(options);
        }
    }
}

void PitchGuideLiveEditor::timerCallback()
{
    view.tick();

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - lastScoreMs > 250.0)
    {
        lastScoreMs = nowMs;
        const auto &options = liveProcessor.getOptions();
        score = liveProcessor.getTrail().score(liveProcessor.getReference(),
                                               options.referenceStartSeconds,
                                               static_cast<float>(liveProcessor.getTranspose()),
                                               options.toleranceCents);
        updateHeader();
        repaint(0, 0, getWidth(), kHeaderHeight);
    }
}

void PitchGuideLiveEditor::updateHeader()
{
    const int transpose = liveProcessor.getTranspose();
    transposeValue.setText((transpose > 0 ? "+" : "") + juce::String(transpose) + " st",
                           juce::dontSendNotification);
    transposeValue.setColour(juce::Label::textColourId,
                             transpose == 0 ? juce::Colours::white : APP_COLOR_PRIMARY.brighter(0.4f));

    juce::String text;
    const auto &reference = liveProcessor.getReference();
    if (liveProcessor.isAnalysing())
        text = liveProcessor.getAnalysisMessage();
    else if (reference.isLoaded())
        text = juce::File(reference.sourcePath).getFileName() + "   (" +
               juce::String(static_cast<int>(reference.notes.size())) + " notes)";
    else
        text = "No reference loaded";
    if (referenceLabel.getText() != text)
        referenceLabel.setText(text, juce::dontSendNotification);
    referenceLabel.setTooltip(reference.sourcePath);
}

void PitchGuideLiveEditor::chooseReference()
{
    const auto &reference = liveProcessor.getReference();
    const auto startDirectory =
        reference.isLoaded() && juce::File::isAbsolutePath(reference.sourcePath)
            ? juce::File(reference.sourcePath).getParentDirectory()
            : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser>(
        "Choose the song's vocal", startDirectory,
        "*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff;*.m4a;*.wma");
    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser &fc) {
                             const auto file = fc.getResult();
                             if (file.existsAsFile())
                                 liveProcessor.loadReference(file);
                             updateHeader();
                         });
}

void PitchGuideLiveEditor::mouseDoubleClick(const juce::MouseEvent &event)
{
    if (event.eventComponent == &transposeValue)
    {
        liveProcessor.setTranspose(0);
        updateHeader();
    }
}

void PitchGuideLiveEditor::showSettings()
{
    juce::CallOutBox::launchAsynchronously(std::make_unique<LiveSettingsPanel>(liveProcessor),
                                           settingsButton.getBounds(), this);
}

bool PitchGuideLiveEditor::isAudioFile(const juce::String &path)
{
    return juce::File(path).hasFileExtension("wav;flac;mp3;ogg;aif;aiff;m4a;wma");
}

bool PitchGuideLiveEditor::isInterestedInFileDrag(const juce::StringArray &files)
{
    return files.size() == 1 && isAudioFile(files[0]);
}

void PitchGuideLiveEditor::fileDragEnter(const juce::StringArray &, int, int)
{
    view.setDropHighlight(true);
}

void PitchGuideLiveEditor::fileDragExit(const juce::StringArray &)
{
    view.setDropHighlight(false);
}

void PitchGuideLiveEditor::filesDropped(const juce::StringArray &files, int, int)
{
    view.setDropHighlight(false);
    if (files.size() == 1 && isAudioFile(files[0]))
        liveProcessor.loadReference(juce::File(files[0]));
    updateHeader();
}
