#pragma once

#include "../Audio/Live/SungPitchTrail.h"
#include "../JuceHeader.h"
#include "KaraokeView.h"

#include <memory>

class PitchGuideLiveProcessor;

/**
 * PitchGuide Live's window: a header with the reference, transpose and the
 * score, over the karaoke roll. The whole window accepts a dropped
 * reference vocal.
 */
class PitchGuideLiveEditor : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
{
public:
    explicit PitchGuideLiveEditor(PitchGuideLiveProcessor &processor);
    ~PitchGuideLiveEditor() override;

    void paint(juce::Graphics &g) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent &event) override;

    bool isInterestedInFileDrag(const juce::StringArray &files) override;
    void fileDragEnter(const juce::StringArray &, int, int) override;
    void fileDragExit(const juce::StringArray &) override;
    void filesDropped(const juce::StringArray &files, int, int) override;

private:
    static constexpr int kHeaderHeight = 52;

    void timerCallback() override;
    void chooseReference();
    void showSettings();
    void updateHeader();
    static bool isAudioFile(const juce::String &path);

    PitchGuideLiveProcessor &liveProcessor;
    KaraokeView view;

    juce::TextButton loadButton{"Load reference..."};
    juce::Label referenceLabel;
    juce::Label transposeCaption;
    juce::TextButton octaveDownButton{"-12"};
    juce::TextButton semitoneDownButton{"-"};
    juce::Label transposeValue;
    juce::TextButton semitoneUpButton{"+"};
    juce::TextButton octaveUpButton{"+12"};
    juce::TextButton clearButton{"Clear"};
    juce::TextButton settingsButton{"Settings"};
    std::unique_ptr<juce::FileChooser> chooser;

    SungPitchTrail::Score score;
    double lastScoreMs = 0.0;
    bool restoringSize = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchGuideLiveEditor)
};
