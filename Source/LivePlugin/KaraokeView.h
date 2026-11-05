#pragma once

#include "../JuceHeader.h"

class PitchGuideLiveProcessor;

/**
 * The karaoke roll: the reference's notes scroll from right to left towards a
 * fixed "now" line, and what the singer sings is drawn behind it, coloured by
 * how close it is to the note.
 *
 * The pitch range follows the song (and the voice when there is no song), so
 * the view never needs scrolling. The mouse wheel zooms time.
 */
class KaraokeView : public juce::Component
{
public:
    explicit KaraokeView(PitchGuideLiveProcessor &processor);

    /** Pulls new frames and the transport from the processor; call at the
        display rate. */
    void tick();

    void paint(juce::Graphics &g) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
    void mouseDoubleClick(const juce::MouseEvent &) override;

    void setDropHighlight(bool shouldHighlight);

    /** "C4", "F#3"... for the nearest note. */
    static juce::String noteName(float midi);

private:
    static constexpr float kKeysWidth = 46.0f;
    static constexpr float kNowFraction = 0.28f;
    static constexpr float kMinSpanSemitones = 15.0f;

    juce::Rectangle<float> rollArea() const;
    float nowX() const;
    float timeToX(double hostSeconds) const;
    float midiToY(float midi) const;
    float semitoneHeight() const;
    double visibleStartSeconds() const;
    double visibleEndSeconds() const;

    /** Target MIDI (transpose applied) at a host time, 0 in a gap. */
    float targetAt(double hostSeconds) const;
    /** KaraokeReference::pitchErrorAt at a host time. */
    float errorAt(double hostSeconds, float sungMidi) const;
    bool hasRecentVoice() const;
    void updatePitchRange();

    void drawLanes(juce::Graphics &g) const;
    void drawTimeGrid(juce::Graphics &g) const;
    void drawNotes(juce::Graphics &g) const;
    void drawSungTrail(juce::Graphics &g, float opacity) const;
    void drawFreeTrail(juce::Graphics &g) const;
    void drawNowLine(juce::Graphics &g) const;
    void drawLiveVoice(juce::Graphics &g) const;
    void drawKeys(juce::Graphics &g) const;
    void drawOverlay(juce::Graphics &g) const;

    PitchGuideLiveProcessor &processor;
    double viewSeconds = 0.0;
    double lastTickMs = 0.0;
    float centreMidi = 60.0f;
    float spanSemitones = 18.0f;
    bool rangeInitialised = false;
    bool dropHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KaraokeView)
};
