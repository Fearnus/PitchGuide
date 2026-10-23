#include "TakeDragHandle.h"
#include "Components/AppFont.h"
#include "../Utils/Localization.h"
#include "../Utils/UI/Theme.h"
#include <cmath>

namespace
{
constexpr int kNoticePadX = 12;
constexpr int kNoticePadY = 8;
constexpr int kNoticeArrow = 7;
constexpr int kNoticeMargin = 8;
constexpr float kNoticeCorner = 6.0f;
constexpr float kNoticeMaxTextWidth = 300.0f;
constexpr double kNoticeShowMs = 3500.0;
} // namespace

TakeDragHandle::TakeDragHandle()
{
    refreshLocalisedText();
}

void TakeDragHandle::setImage(const juce::Image &newSprite)
{
    sprite = newSprite;
    repaint();
}

void TakeDragHandle::setState(TakeExport::State newState)
{
    if (state == newState)
        return;

    state = newState;
    setMouseCursor(state == TakeExport::State::ready
                       ? juce::MouseCursor::DraggingHandCursor
                       : juce::MouseCursor::NormalCursor);
    refreshLocalisedText();
    repaint();
}

void TakeDragHandle::refreshLocalisedText()
{
    switch (state)
    {
    case TakeExport::State::noTake:
        setTooltip(TR("tooltip.drag_take_empty"));
        break;
    case TakeExport::State::rendering:
        setTooltip(TR("tooltip.drag_take_rendering"));
        break;
    case TakeExport::State::ready:
        setTooltip(TR("tooltip.drag_take"));
        break;
    }
}

void TakeDragHandle::paint(juce::Graphics &g)
{
    if (!sprite.isValid())
        return;

    const int row = state != TakeExport::State::ready ? 2
                    : (isMouseOver() || dragging) ? 1
                                                  : 0;
    const int frameH = sprite.getHeight() / 3;
    const int w = (sprite.getWidth() + 1) / 2;
    const int h = (frameH + 1) / 2;
    g.drawImage(sprite, (getWidth() - w) / 2, (getHeight() - h) / 2, w, h,
                0, row * frameH, sprite.getWidth(), frameH);
}

void TakeDragHandle::mouseEnter(const juce::MouseEvent &)
{
    repaint();
}

void TakeDragHandle::mouseExit(const juce::MouseEvent &)
{
    repaint();
}

void TakeDragHandle::mouseDown(const juce::MouseEvent &)
{
    // The OS drag holds the mouse until it ends, so a new press means it has.
    gestureHandled = false;
    dragging = false;
}

void TakeDragHandle::mouseDrag(const juce::MouseEvent &e)
{
    if (gestureHandled || dragging || e.getDistanceFromDragStart() < 4)
        return;
    gestureHandled = true;

    const auto prepared = onPrepare ? onPrepare() : Prepared{};
    if (prepared.file == juce::File())
    {
        if (onProblem && prepared.problem.isNotEmpty())
            onProblem(prepared.problem);
        return;
    }

    // The receiver copies or references the file; it must never move it out
    // from under a project that already uses it.
    juce::Component::SafePointer<TakeDragHandle> safeThis(this);
    dragging = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        {prepared.file.getFullPathName()}, false, this, [safeThis]
        {
            if (safeThis == nullptr)
                return;
            safeThis->dragging = false;
            safeThis->repaint();
        });
    repaint();
}

TakeDragNotice::TakeDragNotice()
{
    setVisible(false);
}

void TakeDragNotice::show(const juce::String &message, juce::Rectangle<int> target)
{
    auto *parent = getParentComponent();
    if (parent == nullptr)
        return;

    juce::AttributedString text;
    text.append(message, AppFont::getFont(14.0f), APP_COLOR_TEXT_PRIMARY);
    text.setJustification(juce::Justification::centred);
    layout.createLayoutWithBalancedLineLengths(text, kNoticeMaxTextWidth);

    const int width = static_cast<int>(std::ceil(layout.getWidth())) + 2 * kNoticePadX;
    const int height = static_cast<int>(std::ceil(layout.getHeight())) + 2 * kNoticePadY +
                       kNoticeArrow;
    const int x = juce::jlimit(kNoticeMargin,
                               juce::jmax(kNoticeMargin, parent->getWidth() - kNoticeMargin - width),
                               target.getCentreX() - width / 2);
    setBounds(x, target.getBottom() + 2, width, height);

    const float arrowRoom = kNoticeCorner + kNoticeArrow;
    arrowX = juce::jlimit(arrowRoom, static_cast<float>(width) - arrowRoom,
                          static_cast<float>(target.getCentreX() - x));

    clicksWhenShown = juce::Desktop::getInstance().getMouseButtonClickCounter();
    hideAtMs = juce::Time::getMillisecondCounterHiRes() + kNoticeShowMs;
    setAlpha(1.0f);
    setVisible(true);
    toFront(false);
    repaint();
    startTimer(100);
}

void TakeDragNotice::paint(juce::Graphics &g)
{
    const auto body = getLocalBounds().toFloat().withTrimmedTop(kNoticeArrow).reduced(0.5f);
    juce::Path bubble;
    bubble.addBubble(body, getLocalBounds().toFloat(), {arrowX, 0.5f}, kNoticeCorner,
                     2.0f * kNoticeArrow);
    g.setColour(juce::Colour(0xFF232323u));
    g.fillPath(bubble);
    g.setColour(juce::Colour(0xFF3C3C3Cu));
    g.strokePath(bubble, juce::PathStrokeType(1.0f));

    layout.draw(g, body.reduced(static_cast<float>(kNoticePadX), static_cast<float>(kNoticePadY)));
}

void TakeDragNotice::mouseDown(const juce::MouseEvent &)
{
    dismiss();
}

void TakeDragNotice::timerCallback()
{
    // The press that started the drag was counted before show(), so any
    // change means a new click somewhere.
    if (juce::Time::getMillisecondCounterHiRes() >= hideAtMs ||
        juce::Desktop::getInstance().getMouseButtonClickCounter() != clicksWhenShown)
        dismiss();
}

void TakeDragNotice::dismiss()
{
    stopTimer();
    if (isVisible())
        juce::Desktop::getInstance().getAnimator().fadeOut(this, 150);
}
