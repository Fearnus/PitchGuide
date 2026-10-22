#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/ReferenceMatch.h"
#include <functional>

namespace MatchReferenceDialog {
// Popup with the Match Reference settings: note pitch % (default 100),
// expression % (default 0), timing % (default 0) and transpose in semitones
// (-12..12), starting at `initial`. Opening it changes nothing. onPreview is
// called with every change, also while a slider is dragged; onRefreshPreview
// after a slider is released (or a change made without dragging), when the
// edit should be rendered; onComplete with true only when the user confirms.
void showPopup(juce::Component *parent, juce::Rectangle<int> anchorBounds,
               const ReferenceMatch::Settings &initial,
               std::function<void(const ReferenceMatch::Settings &)> onPreview,
               std::function<void(bool)> onComplete,
               std::function<void()> onRefreshPreview);
// Cancels the active popup, if any.
void dismissPopup();
} // namespace MatchReferenceDialog
