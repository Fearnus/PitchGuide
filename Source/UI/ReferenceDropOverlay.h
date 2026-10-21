#pragma once

#include "../JuceHeader.h"

/**
 * Shown over the editor while audio files are dragged onto it: two targets,
 * one to use the file as the reference vocal and one to use it as the take.
 * It never takes mouse clicks; the editor asks it which target a position
 * falls in.
 */
class ReferenceDropOverlay : public juce::Component
{
public:
  enum class Target
  {
    None,
    Reference,
    Take
  };

  ReferenceDropOverlay();

  /** The target under `position` (in this component's coordinates). */
  Target targetAt(juce::Point<int> position) const;

  /** Highlights the target under `position`. */
  void setHoverPosition(juce::Point<int> position);

  void paint(juce::Graphics &g) override;

private:
  juce::Rectangle<int> getTargetBounds(Target target) const;
  void drawTarget(juce::Graphics &g, Target target, const juce::String &title,
                  const juce::String &hint) const;

  Target hovered = Target::None;
};
