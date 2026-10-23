#pragma once

#include "../JuceHeader.h"
#include "../Utils/TakeExport.h"
#include <functional>

/**
 * The toolbar handle the corrected take is dragged out of, into a DAW.
 *
 * A drag that starts here asks `onPrepare` for the take as a WAV file and
 * hands that file to the operating system's drag and drop. While the take is
 * still rendering the handle is dimmed and a drag reports why it cannot start
 * through `onProblem` instead.
 */
class TakeDragHandle final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    struct Prepared
    {
        juce::File file;      // empty when there is nothing to drag
        juce::String problem; // why, for the user
    };

    TakeDragHandle();

    /** Three rows at 2x, as Button uses: normal, hover, unavailable. */
    void setImage(const juce::Image &sprite);
    void setState(TakeExport::State newState);
    TakeExport::State getState() const { return state; }
    void refreshLocalisedText();

    std::function<Prepared()> onPrepare;
    std::function<void(const juce::String &)> onProblem;

    void paint(juce::Graphics &g) override;
    void mouseEnter(const juce::MouseEvent &e) override;
    void mouseExit(const juce::MouseEvent &e) override;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseDrag(const juce::MouseEvent &e) override;

private:
    juce::Image sprite;
    TakeExport::State state = TakeExport::State::noTake;
    bool gestureHandled = false; // one drag attempt per press
    bool dragging = false;       // the OS drag is still running

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TakeDragHandle)
};

/**
 * Why a drag could not start, shown under the handle for a few seconds or
 * until the next click. Unlike juce::BubbleMessageComponent it stays inside
 * its parent, which matters for a handle at the window's right edge.
 */
class TakeDragNotice final : public juce::Component, private juce::Timer
{
public:
    TakeDragNotice();

    /** Shows `message` pointing at `target`, in the parent's coordinates. */
    void show(const juce::String &message, juce::Rectangle<int> target);

    void paint(juce::Graphics &g) override;
    void mouseDown(const juce::MouseEvent &e) override;

private:
    void timerCallback() override;
    void dismiss();

    juce::TextLayout layout;
    float arrowX = 0.0f;
    int clicksWhenShown = 0;
    double hideAtMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TakeDragNotice)
};
