#include "ReferenceDropOverlay.h"

#include "../Utils/Localization.h"
#include "../Utils/UI/Theme.h"
#include "Components/AppFont.h"

ReferenceDropOverlay::ReferenceDropOverlay()
{
  setInterceptsMouseClicks(false, false);
  setVisible(false);
}

juce::Rectangle<int> ReferenceDropOverlay::getTargetBounds(Target target) const
{
  // Reference on the left, take on the right, with a margin around both.
  auto area = getLocalBounds().reduced(juce::jmax(16, getWidth() / 24),
                                       juce::jmax(16, getHeight() / 8));
  const int gap = juce::jmax(12, area.getWidth() / 40);
  const int half = (area.getWidth() - gap) / 2;
  if (target == Target::Reference)
    return area.removeFromLeft(half);
  if (target == Target::Take)
    return area.removeFromRight(half);
  return {};
}

ReferenceDropOverlay::Target
ReferenceDropOverlay::targetAt(juce::Point<int> position) const
{
  // Each half of the window belongs to its target, margins included, so a
  // drop never lands on nothing.
  return position.x < getWidth() / 2 ? Target::Reference : Target::Take;
}

void ReferenceDropOverlay::setHoverPosition(juce::Point<int> position)
{
  const auto target = targetAt(position);
  if (target != hovered)
  {
    hovered = target;
    repaint();
  }
}

void ReferenceDropOverlay::drawTarget(juce::Graphics &g, Target target,
                                      const juce::String &title,
                                      const juce::String &hint) const
{
  const auto bounds = getTargetBounds(target).toFloat();
  const bool active = hovered == target;

  g.setColour(active ? APP_COLOR_PRIMARY.withAlpha(0.28f)
                     : APP_COLOR_SURFACE_RAISED.withAlpha(0.85f));
  g.fillRoundedRectangle(bounds, 12.0f);
  g.setColour(active ? APP_COLOR_PRIMARY : APP_COLOR_BORDER_HIGHLIGHT);
  g.drawRoundedRectangle(bounds.reduced(1.0f), 12.0f, active ? 2.5f : 1.5f);

  auto text = bounds.reduced(16.0f).toNearestInt();
  const int titleHeight = 30;
  auto titleArea = text.withSizeKeepingCentre(text.getWidth(), titleHeight)
                       .translated(0, -titleHeight / 2);
  g.setColour(APP_COLOR_TEXT_PRIMARY);
  g.setFont(AppFont::getBoldFont(22.0f));
  g.drawFittedText(title, titleArea, juce::Justification::centred, 1);

  g.setColour(APP_COLOR_TEXT_MUTED);
  g.setFont(AppFont::getFont(14.0f));
  g.drawFittedText(hint, titleArea.translated(0, titleHeight),
                   juce::Justification::centred, 2);
}

void ReferenceDropOverlay::paint(juce::Graphics &g)
{
  g.fillAll(APP_COLOR_OVERLAY_DIM.withAlpha(0.75f));
  drawTarget(g, Target::Reference, TR("drop.reference"), TR("drop.reference_hint"));
  drawTarget(g, Target::Take, TR("drop.take"), TR("drop.take_hint"));
}
