#include "MatchReferenceDialog.h"
#include "../Components/AppFont.h"
#include "../Components/DarkLookAndFeel.h"
#include "../Components/StyledWidgets.h"
#include "../../Utils/Localization.h"
#include "../../Utils/UI/Theme.h"
#include <utility>

namespace MatchReferenceDialog {
namespace {

class Content final : public juce::Component
{
public:
  Content(const ReferenceMatch::Settings &initial,
          std::function<void(const ReferenceMatch::Settings &)> preview,
          std::function<void(bool)> completed, std::function<void()> refreshPreview)
      : onPreview(std::move(preview)), onComplete(std::move(completed)),
        onRefreshPreview(std::move(refreshPreview))
  {
    // Double-click returns a slider to its default, not to where it opened.
    const ReferenceMatch::Settings defaults;
    setupRow(notePitchLabel, notePitch, TR("match.note_pitch"), 0.0, 100.0, " %",
             initial.notePitch * 100.0, defaults.notePitch * 100.0);
    setupRow(expressionLabel, expression, TR("match.expression"), 0.0, 100.0, " %",
             initial.expression * 100.0, defaults.expression * 100.0);
    setupRow(timingLabel, timing, TR("match.timing"), 0.0, 100.0, " %",
             initial.timing * 100.0, defaults.timing * 100.0);
    setupRow(transposeLabel, transpose, TR("match.transpose"), -12.0, 12.0, " st",
             initial.transpose, defaults.transpose);

    setupButton(cancelButton, TR("dialog.cancel"));
    setupButton(okButton, TR("dialog.ok"));
    cancelButton.onClick = [this] { finish(false); };
    okButton.onClick = [this] { finish(true); };
    addAndMakeVisible(cancelButton);
    addAndMakeVisible(okButton);
  }

  ~Content() override
  {
    // Covers the popup being torn down with its parent, not just Cancel.
    if (!finished && onComplete)
      onComplete(false);
  }

  void cancel() { finish(false); }

  void paint(juce::Graphics &g) override
  {
    g.setColour(juce::Colour(0xFF181818));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
    g.setColour(APP_COLOR_BORDER.withAlpha(0.55f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 10.0f, 1.0f);
  }

  void resized() override
  {
    auto area = getLocalBounds().reduced(16, 10);
    for (auto row : {std::pair<juce::Label *, MacroSlider *>{&notePitchLabel, &notePitch},
                     {&expressionLabel, &expression},
                     {&timingLabel, &timing},
                     {&transposeLabel, &transpose}})
    {
      auto bounds = area.removeFromTop(32);
      row.first->setBounds(bounds.removeFromLeft(94));
      row.second->setBounds(bounds);
      area.removeFromTop(6);
    }
    area.removeFromTop(4);
    auto buttons = area.removeFromTop(24).removeFromRight(160).translated(1, 0);
    cancelButton.setBounds(buttons.removeFromLeft(75));
    buttons.removeFromLeft(10);
    okButton.setBounds(buttons);
  }

private:
  void setupRow(juce::Label &label, MacroSlider &slider, const juce::String &text,
                double minimum, double maximum, const juce::String &suffix,
                double initial, double reset)
  {
    label.setText(text, juce::dontSendNotification);
    label.setFont(AppFont::getFont(15.0f));
    label.setColour(juce::Label::textColourId, APP_COLOR_TEXT_PRIMARY);
    slider.setRange(minimum, maximum, 1.0);
    slider.setTextValueSuffix(suffix);
    slider.setValue(initial, juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, reset);
    slider.onDragStart = [this] { sliderDragging = true; };
    slider.onDragEnd = [this] { sliderDragging = false; refreshMatch(); };
    slider.onValueChange = [this] { previewMatch(); };
    addAndMakeVisible(label);
    addAndMakeVisible(slider);
  }

  static void setupButton(juce::TextButton &button, const juce::String &text)
  {
    button.setButtonText(text);
    button.setLookAndFeel(&DarkLookAndFeel::getInstance());
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF5B5B5B));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF3E3E3E));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFEFEFEF));
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  }

  ReferenceMatch::Settings currentSettings() const
  {
    ReferenceMatch::Settings settings;
    settings.notePitch = static_cast<float>(notePitch.getValue() / 100.0);
    settings.expression = static_cast<float>(expression.getValue() / 100.0);
    settings.transpose = static_cast<float>(transpose.getValue());
    settings.timing = static_cast<float>(timing.getValue() / 100.0);
    return settings;
  }

  void finish(bool accepted)
  {
    if (finished)
      return;
    if (accepted)
      refreshMatch();
    finished = true;
    if (onComplete)
      onComplete(accepted);
    auto safeThis = juce::Component::SafePointer<Content>(this);
    juce::MessageManager::callAsync([safeThis]
    {
      if (auto *content = safeThis.getComponent())
      {
        if (auto *parent = content->getParentComponent())
          parent->removeChildComponent(content);
        delete content;
      }
    });
  }

  void previewMatch()
  {
    if (finished)
      return;
    if (onPreview)
      onPreview(currentSettings());
    previewPending = true;
    if (!sliderDragging)
      refreshMatch();
  }

  void refreshMatch()
  {
    if (!previewPending || finished)
      return;
    previewPending = false;
    if (onRefreshPreview)
      onRefreshPreview();
  }

  std::function<void(const ReferenceMatch::Settings &)> onPreview;
  std::function<void(bool)> onComplete;
  std::function<void()> onRefreshPreview;
  bool sliderDragging = false;
  bool previewPending = false;
  juce::Label notePitchLabel, expressionLabel, timingLabel, transposeLabel;
  MacroSlider notePitch, expression, timing, transpose;
  juce::TextButton cancelButton, okButton;
  bool finished = false;
};

juce::Component::SafePointer<Content> activePopup;
} // namespace

void dismissPopup()
{
  if (auto *content = activePopup.getComponent())
    content->cancel();
}

void showPopup(juce::Component *parent, juce::Rectangle<int> anchorBounds,
               const ReferenceMatch::Settings &initial,
               std::function<void(const ReferenceMatch::Settings &)> onPreview,
               std::function<void(bool)> onComplete,
               std::function<void()> onRefreshPreview)
{
  if (parent == nullptr)
    return;

  dismissPopup();

  constexpr int width = 340;
  constexpr int height = 190;
  auto *content = new Content(initial, std::move(onPreview), std::move(onComplete),
                              std::move(onRefreshPreview));
  content->setSize(width, height);
  const auto bounds = parent->getLocalBounds();
  const int x = juce::jlimit(8, juce::jmax(8, bounds.getWidth() - width - 8),
                             anchorBounds.getRight() - width);
  const int y = juce::jlimit(0, juce::jmax(0, bounds.getHeight() - height),
                             anchorBounds.getY());
  parent->addAndMakeVisible(content);
  content->setBounds(x, y, width, height);
  content->toFront(false);
  activePopup = content;
}

} // namespace MatchReferenceDialog
