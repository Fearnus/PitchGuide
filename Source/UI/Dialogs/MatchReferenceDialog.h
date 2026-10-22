#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/ReferenceMatch.h"
#include <functional>

namespace MatchReferenceDialog {
// Popup with the Match Reference settings: note pitch % (default 100),
// expression % (default 0) and transpose in semitones (-12..12).
// onPreview is called with every change, also while a slider is dragged;
// onRefreshPreview after a slider is released (or a change made without
// dragging), when the edit should be rendered; onComplete with true only when
// the user confirms. The popup previews its defaults as soon as it opens.
void showPopup(juce::Component *parent, juce::Rectangle<int> anchorBounds,
               std::function<void(const ReferenceMatch::Settings &)> onPreview,
               std::function<void(bool)> onComplete,
               std::function<void()> onRefreshPreview);
// Cancels the active popup, if any.
void dismissPopup();
} // namespace MatchReferenceDialog
