#include "KaraokeView.h"

#include "../Audio/Live/SungPitchTrail.h"
#include "../UI/Components/AppFont.h"
#include "../Utils/UI/Theme.h"
#include "LiveProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const juce::Colour kBackground(0xFF14161Cu);
const juce::Colour kLaneWhite(0xFF191C24u);
const juce::Colour kLaneBlack(0xFF111319u);
const juce::Colour kNoteFill(0xFF3B4457u);
const juce::Colour kNoteOutline(0xFF8792A8u);
const juce::Colour kNoteCurrent(0xFF59667Fu);
const juce::Colour kInTune(0xFF3DDC84u);
const juce::Colour kNear(0xFFFFD34Eu);
const juce::Colour kOff(0xFFFF5C5Cu);
const juce::Colour kNoTarget(0xFFB9A8FFu);
const juce::Colour kLyricsBackground(0xFF0C0E12u);
const juce::Colour kLyricsUnsung(0xFF6E7587u);
const juce::Colour kLyricsNext(0xFF464C5Au);

constexpr double kLiveVoiceMaxAgeMs = 150.0;

bool isBlackKey(int midi)
{
    switch (((midi % 12) + 12) % 12)
    {
    case 1: case 3: case 6: case 8: case 10:
        return true;
    default:
        return false;
    }
}

enum class Accuracy
{
    NoTarget,
    InTune,
    Near,
    Off
};

/** errorSemitones as KaraokeReference::pitchErrorAt reports it: empty = no note. */
Accuracy classify(std::optional<float> errorSemitones, float toleranceSemitones)
{
    if (!errorSemitones)
        return Accuracy::NoTarget;
    const float distance = std::abs(*errorSemitones);
    if (distance <= toleranceSemitones)
        return Accuracy::InTune;
    if (distance <= 2.0f * toleranceSemitones)
        return Accuracy::Near;
    return Accuracy::Off;
}

juce::Colour colourFor(Accuracy accuracy)
{
    switch (accuracy)
    {
    case Accuracy::InTune: return kInTune;
    case Accuracy::Near: return kNear;
    case Accuracy::Off: return kOff;
    case Accuracy::NoTarget: break;
    }
    return kNoTarget;
}
} // namespace

KaraokeView::KaraokeView(PitchGuideLiveProcessor &p) : processor(p)
{
    setOpaque(true);
}

juce::String KaraokeView::noteName(float midi)
{
    static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int note = static_cast<int>(std::lround(midi));
    const int pitchClass = ((note % 12) + 12) % 12;
    const int octave = static_cast<int>(std::floor(note / 12.0)) - 1;
    return juce::String(names[pitchClass]) + juce::String(octave);
}

void KaraokeView::setDropHighlight(bool shouldHighlight)
{
    if (dropHighlight != shouldHighlight)
    {
        dropHighlight = shouldHighlight;
        repaint();
    }
}

// ---- Geometry --------------------------------------------------------------

float KaraokeView::lyricsBandHeight() const
{
    if (!processor.getLyrics().isTimed())
        return 0.0f;
    return juce::jlimit(64.0f, 150.0f, static_cast<float>(getHeight()) * 0.24f);
}

juce::Rectangle<float> KaraokeView::rollArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft(kKeysWidth).withTrimmedBottom(
        lyricsBandHeight());
}

juce::Rectangle<float> KaraokeView::lyricsArea() const
{
    auto bounds = getLocalBounds().toFloat();
    return bounds.removeFromBottom(lyricsBandHeight());
}

float KaraokeView::nowX() const
{
    const auto roll = rollArea();
    return roll.getX() + roll.getWidth() * kNowFraction;
}

float KaraokeView::timeToX(double hostSeconds) const
{
    return nowX() + static_cast<float>((hostSeconds - viewSeconds) *
                                       processor.getOptions().pixelsPerSecond);
}

float KaraokeView::semitoneHeight() const
{
    return rollArea().getHeight() / std::max(1.0f, spanSemitones);
}

float KaraokeView::midiToY(float midi) const
{
    return rollArea().getCentreY() - (midi - centreMidi) * semitoneHeight();
}

double KaraokeView::visibleStartSeconds() const
{
    return viewSeconds - (nowX() - rollArea().getX()) / processor.getOptions().pixelsPerSecond;
}

double KaraokeView::visibleEndSeconds() const
{
    return viewSeconds + (rollArea().getRight() - nowX()) / processor.getOptions().pixelsPerSecond;
}

float KaraokeView::targetAt(double hostSeconds) const
{
    const auto &reference = processor.getReference();
    const auto *note = reference.noteAt(hostSeconds - processor.getOptions().referenceStartSeconds);
    return note != nullptr ? note->midi + static_cast<float>(processor.getTranspose()) : 0.0f;
}

std::optional<float> KaraokeView::errorAt(double hostSeconds, float sungMidi) const
{
    return processor.getReference().pitchErrorAt(
        hostSeconds - processor.getOptions().referenceStartSeconds, sungMidi,
        static_cast<float>(processor.getTranspose()));
}

bool KaraokeView::hasRecentVoice() const
{
    return processor.getLatestFrame().isVoiced() &&
           juce::Time::getMillisecondCounterHiRes() - processor.getLatestFrameWallMs() <
               kLiveVoiceMaxAgeMs;
}

// ---- Update ----------------------------------------------------------------

void KaraokeView::tick()
{
    processor.drainLiveFrames();

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double target = processor.estimateHostSeconds();
    if (processor.isHostPlaying() && lastTickMs > 0.0)
    {
        // Advance with the wall clock and ease towards the host position, so
        // block-sized jumps in the reported position do not shake the roll.
        viewSeconds += (nowMs - lastTickMs) / 1000.0;
        const double error = target - viewSeconds;
        if (std::abs(error) > 0.15)
            viewSeconds = target;
        else
            viewSeconds += error * 0.2;
    }
    else
    {
        viewSeconds = target;
    }
    lastTickMs = nowMs;

    updatePitchRange();
    repaint();
}

void KaraokeView::updatePitchRange()
{
    const auto &reference = processor.getReference();
    const float transpose = static_cast<float>(processor.getTranspose());
    const double referenceStart = processor.getOptions().referenceStartSeconds;

    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    if (reference.isLoaded())
    {
        const double from = visibleStartSeconds() - referenceStart;
        const double to = visibleEndSeconds() - referenceStart + 2.0;
        for (size_t i = reference.firstNoteEndingAfter(from);
             i < reference.notes.size() && reference.notes[i].startSeconds < to; ++i)
        {
            low = std::min(low, reference.notes[i].midi + transpose);
            high = std::max(high, reference.notes[i].midi + transpose);
        }
    }

    const bool voice = hasRecentVoice();
    const float voiceMidi = processor.getLatestFrame().midi;
    if (low > high)
    {
        if (!voice)
            return;
        low = high = voiceMidi;
    }
    else if (voice && voiceMidi > low - 7.0f && voiceMidi < high + 7.0f)
    {
        // Keep the singer on screen when they drift, but not when they are
        // in another octave altogether: that is what transpose is for.
        low = std::min(low, voiceMidi);
        high = std::max(high, voiceMidi);
    }

    const float targetSpan = std::max(kMinSpanSemitones, high - low + 6.0f);
    const float targetCentre = 0.5f * (low + high);
    if (!rangeInitialised)
    {
        centreMidi = targetCentre;
        spanSemitones = targetSpan;
        rangeInitialised = true;
        return;
    }
    centreMidi += (targetCentre - centreMidi) * 0.06f;
    spanSemitones += (targetSpan - spanSemitones) * 0.06f;
}

// ---- Input -----------------------------------------------------------------

void KaraokeView::mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &wheel)
{
    auto options = processor.getOptions();
    options.pixelsPerSecond = juce::jlimit(
        40.0f, 600.0f, options.pixelsPerSecond * std::exp(wheel.deltaY * 0.6f));
    processor.setOptions(options);
    repaint();
}

void KaraokeView::mouseDoubleClick(const juce::MouseEvent &)
{
    auto options = processor.getOptions();
    options.pixelsPerSecond = PitchGuideLiveProcessor::Options{}.pixelsPerSecond;
    processor.setOptions(options);
    repaint();
}

// ---- Painting --------------------------------------------------------------

void KaraokeView::paint(juce::Graphics &g)
{
    g.fillAll(kBackground);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(rollArea().toNearestInt());
        drawLanes(g);
        drawTimeGrid(g);
        drawNotes(g);
        drawSectionLabels(g);
        // Stopped, the recorded attempt steps back behind the tuner trail.
        const bool playing = processor.isHostPlaying();
        drawSungTrail(g, playing ? 1.0f : 0.35f);
        if (!playing)
            drawFreeTrail(g);
        drawNowLine(g);
        drawLiveVoice(g);
    }
    drawKeys(g);
    drawLyrics(g);
    drawOverlay(g);
}

void KaraokeView::drawLanes(juce::Graphics &g) const
{
    const auto roll = rollArea();
    const float rowHeight = semitoneHeight();
    const int lowest = static_cast<int>(std::floor(centreMidi - 0.5f * spanSemitones)) - 1;
    const int highest = static_cast<int>(std::ceil(centreMidi + 0.5f * spanSemitones)) + 1;
    for (int note = lowest; note <= highest; ++note)
    {
        const float top = midiToY(static_cast<float>(note) + 0.5f);
        g.setColour(isBlackKey(note) ? kLaneBlack : kLaneWhite);
        g.fillRect(roll.getX(), top, roll.getWidth(), rowHeight);
        // Line under each C (between B and C) is stronger.
        const float bottom = top + rowHeight;
        g.setColour(juce::Colours::white.withAlpha(((note % 12) + 12) % 12 == 0 ? 0.12f : 0.035f));
        g.drawHorizontalLine(static_cast<int>(std::round(bottom)), roll.getX(), roll.getRight());
    }
}

void KaraokeView::drawTimeGrid(juce::Graphics &g) const
{
    const auto roll = rollArea();
    const float pixelsPerSecond = processor.getOptions().pixelsPerSecond;
    const double step = pixelsPerSecond < 70.0f ? 2.0 : 1.0;
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    for (double t = std::ceil(visibleStartSeconds() / step) * step; t < visibleEndSeconds();
         t += step)
        g.drawVerticalLine(static_cast<int>(std::round(timeToX(t))), roll.getY(), roll.getBottom());
}

void KaraokeView::drawNotes(juce::Graphics &g) const
{
    const auto &reference = processor.getReference();
    if (!reference.isLoaded())
        return;

    const auto &options = processor.getOptions();
    const float transpose = static_cast<float>(processor.getTranspose());
    const double start = options.referenceStartSeconds;
    const double from = visibleStartSeconds() - start;
    const double to = visibleEndSeconds() - start;
    const float rowHeight = semitoneHeight();
    const float barHeight = std::max(6.0f, rowHeight * 0.72f);
    const float voice = processor.getLatestFrame().midi;
    const bool singing = hasRecentVoice();
    const float tolerance = options.toleranceCents / 100.0f;

    for (size_t i = reference.firstNoteEndingAfter(from);
         i < reference.notes.size() && reference.notes[i].startSeconds < to; ++i)
    {
        const auto &note = reference.notes[i];
        const float target = note.midi + transpose;
        const float x1 = timeToX(note.startSeconds + start);
        const float x2 = timeToX(note.endSeconds + start);
        const float y = midiToY(target);
        juce::Rectangle<float> bar(x1, y - 0.5f * barHeight, std::max(2.0f, x2 - x1), barHeight);

        const bool past = note.endSeconds + start < viewSeconds;
        const bool current = !past && note.startSeconds + start <= viewSeconds;
        const float alpha = past ? 0.4f : 1.0f;
        const float corner = std::min(4.0f, 0.5f * barHeight);

        g.setColour((current ? kNoteCurrent : kNoteFill).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(bar, corner);

        juce::Colour outline = kNoteOutline.withMultipliedAlpha(alpha);
        float outlineWidth = 1.0f;
        if (current)
        {
            outline = singing ? colourFor(classify(errorAt(viewSeconds, voice), tolerance))
                              : juce::Colour(0xFFC9D2E3u);
            outlineWidth = 2.0f;
            g.setColour(outline.withAlpha(0.18f));
            g.fillRoundedRectangle(bar.expanded(4.0f), corner + 4.0f);
        }
        g.setColour(outline);
        g.drawRoundedRectangle(bar, corner, outlineWidth);
    }

    drawSyllableLabels(g, barHeight);

    // The reference's own pitch, thin, over the bars: slides and vibrato.
    if (reference.frameSeconds <= 0.0 || reference.curveMidi.empty())
        return;
    const int firstFrame = std::max(0, static_cast<int>(std::floor(from / reference.frameSeconds)));
    const int lastFrame = std::min(static_cast<int>(reference.curveMidi.size()) - 1,
                                   static_cast<int>(std::ceil(to / reference.frameSeconds)));
    juce::Path curve;
    bool drawing = false;
    float previous = 0.0f;
    for (int frame = firstFrame; frame <= lastFrame; ++frame)
    {
        const float midi = reference.curveMidi[static_cast<size_t>(frame)];
        if (midi <= 0.0f || (drawing && std::abs(midi - previous) > 2.5f))
        {
            drawing = false;
            if (midi <= 0.0f)
                continue;
        }
        const float x = timeToX(frame * reference.frameSeconds + start);
        const float y = midiToY(midi + transpose);
        if (drawing)
            curve.lineTo(x, y);
        else
            curve.startNewSubPath(x, y);
        drawing = true;
        previous = midi;
    }
    g.setColour(APP_COLOR_REFERENCE_CURVE.withAlpha(0.7f));
    g.strokePath(curve, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void KaraokeView::drawSungTrail(juce::Graphics &g, float opacity) const
{
    const auto &trail = processor.getTrail();
    if (trail.isEmpty())
        return;

    const float tolerance = processor.getOptions().toleranceCents / 100.0f;
    const int first = std::max(0, SungPitchTrail::slotAt(visibleStartSeconds()));
    const int last = std::min(trail.numSlots() - 1, SungPitchTrail::slotAt(visibleEndSeconds()));

    // One path per colour, and the part ahead of the now line (an earlier
    // attempt after seeking back) fainter.
    juce::Path paths[2][4];
    float previous = 0.0f;
    float previousX = 0.0f;
    float previousY = 0.0f;
    for (int slot = first; slot <= last; ++slot)
    {
        const float sung = trail.slotValue(slot);
        if (!(sung > 0.0f))
        {
            previous = 0.0f;
            continue;
        }
        const double time = SungPitchTrail::slotTime(slot);
        const float x = timeToX(time);
        const float y = midiToY(sung);
        if (previous > 0.0f && std::abs(sung - previous) < 1.5f)
        {
            const auto accuracy = classify(errorAt(time, sung), tolerance);
            auto &path = paths[time > viewSeconds ? 1 : 0][static_cast<int>(accuracy)];
            path.startNewSubPath(previousX, previousY);
            path.lineTo(x, y);
        }
        previous = sung;
        previousX = x;
        previousY = y;
    }

    const juce::PathStrokeType stroke(3.0f, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);
    for (int ahead = 0; ahead < 2; ++ahead)
        for (int accuracy = 0; accuracy < 4; ++accuracy)
        {
            if (paths[ahead][accuracy].isEmpty())
                continue;
            g.setColour(colourFor(static_cast<Accuracy>(accuracy))
                            .withAlpha((ahead ? 0.3f : 0.95f) * opacity));
            g.strokePath(paths[ahead][accuracy], stroke);
        }
}

void KaraokeView::drawFreeTrail(juce::Graphics &g) const
{
    // Transport stopped: the voice still draws a short trail into the past
    // on the input's own clock, against the note under the playhead.
    const float pixelsPerSecond = processor.getOptions().pixelsPerSecond;
    const float tolerance = processor.getOptions().toleranceCents / 100.0f;
    const double latest = processor.getLatestFrame().streamSeconds;
    const double maxAge = (nowX() - rollArea().getX()) / pixelsPerSecond;

    juce::Path paths[4];
    bool havePrevious = false;
    float previous = 0.0f;
    juce::Point<float> previousPoint;
    processor.forEachRecentFrame([&](const LivePitchFrame &frame) {
        const double age = latest - frame.streamSeconds;
        // Only what was sung since the transport stopped: earlier frames
        // belong to other notes of the song.
        if (age > maxAge || !frame.isVoiced() || frame.hostPlaying)
        {
            havePrevious = false;
            return;
        }
        const juce::Point<float> point(nowX() - static_cast<float>(age) * pixelsPerSecond,
                                       midiToY(frame.midi));
        if (havePrevious && std::abs(frame.midi - previous) < 1.5f)
        {
            auto &path = paths[static_cast<int>(classify(errorAt(viewSeconds, frame.midi), tolerance))];
            path.startNewSubPath(previousPoint);
            path.lineTo(point);
        }
        havePrevious = true;
        previous = frame.midi;
        previousPoint = point;
    });

    const juce::PathStrokeType stroke(3.0f, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);
    for (int accuracy = 0; accuracy < 4; ++accuracy)
    {
        if (paths[accuracy].isEmpty())
            continue;
        g.setColour(colourFor(static_cast<Accuracy>(accuracy)).withAlpha(0.9f));
        g.strokePath(paths[accuracy], stroke);
    }
}

void KaraokeView::drawNowLine(juce::Graphics &g) const
{
    const auto roll = rollArea();
    const float x = nowX();
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.0f), x - 40.0f,
                                           0.0f, juce::Colours::white.withAlpha(0.05f), x,
                                           0.0f, false));
    g.fillRect(x - 40.0f, roll.getY(), 40.0f, roll.getHeight());
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.drawVerticalLine(static_cast<int>(std::round(x)), roll.getY(), roll.getBottom());
}

void KaraokeView::drawLiveVoice(juce::Graphics &g) const
{
    if (!hasRecentVoice())
        return;

    const float sung = processor.getLatestFrame().midi;
    const auto error = errorAt(viewSeconds, sung);
    const float tolerance = processor.getOptions().toleranceCents / 100.0f;
    const auto colour = colourFor(classify(error, tolerance));
    const juce::Point<float> centre(nowX(), midiToY(sung));

    g.setColour(colour.withAlpha(0.22f));
    g.fillEllipse(juce::Rectangle<float>(26.0f, 26.0f).withCentre(centre));
    g.setColour(colour);
    g.fillEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(centre));

    // Cents from the reference, or from the nearest semitone without one.
    const float offset = error ? *error : sung - std::round(sung);
    const int cents = juce::jlimit(-999, 999, static_cast<int>(std::lround(offset * 100.0f)));
    const juce::String text = noteName(sung) + "  " + (cents > 0 ? "+" : "") + juce::String(cents) +
                              juce::String::fromUTF8("\xc2\xa2");
    const auto font = AppFont::getFont(13.0f);
    const float width = juce::GlyphArrangement::getStringWidth(font, text) + 14.0f;
    juce::Rectangle<float> pill(centre.x + 16.0f, centre.y - 11.0f, width, 22.0f);
    if (pill.getRight() > rollArea().getRight())
        pill.setX(centre.x - 16.0f - width);
    g.setColour(juce::Colour(0xE0101218u));
    g.fillRoundedRectangle(pill, 11.0f);
    g.setColour(colour);
    g.drawRoundedRectangle(pill, 11.0f, 1.0f);
    g.setFont(font);
    g.setColour(juce::Colours::white);
    g.drawText(text, pill, juce::Justification::centred);
}

void KaraokeView::drawKeys(juce::Graphics &g) const
{
    const auto roll = rollArea();
    const juce::Rectangle<float> keys(0.0f, roll.getY(), kKeysWidth, roll.getHeight());
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(keys.toNearestInt());
    g.setColour(juce::Colour(0xFF0E1015u));
    g.fillRect(keys);

    const float rowHeight = semitoneHeight();
    const int lowest = static_cast<int>(std::floor(centreMidi - 0.5f * spanSemitones)) - 1;
    const int highest = static_cast<int>(std::ceil(centreMidi + 0.5f * spanSemitones)) + 1;
    const bool voice = hasRecentVoice();
    const int voiceNote = static_cast<int>(std::lround(processor.getLatestFrame().midi));
    const float target = targetAt(viewSeconds);
    const int targetNote = target > 0.0f ? static_cast<int>(std::lround(target)) : -1000;
    const float tolerance = processor.getOptions().toleranceCents / 100.0f;

    g.setFont(AppFont::getFont(std::min(11.0f, std::max(8.0f, rowHeight - 3.0f))));
    for (int note = lowest; note <= highest; ++note)
    {
        const float top = midiToY(static_cast<float>(note) + 0.5f);
        auto key = juce::Rectangle<float>(keys.getX(), top, kKeysWidth - 1.0f, rowHeight)
                       .reduced(0.0f, 0.5f);
        const bool black = isBlackKey(note);
        g.setColour(black ? juce::Colour(0xFF22252Eu) : juce::Colour(0xFFCDD2DCu));
        g.fillRect(black ? key.withTrimmedRight(12.0f) : key);

        if (voice && note == voiceNote)
        {
            g.setColour(colourFor(classify(errorAt(viewSeconds, processor.getLatestFrame().midi),
                                           tolerance))
                            .withAlpha(0.85f));
            g.fillRect(key);
        }
        if (note == targetNote)
        {
            g.setColour(juce::Colours::white);
            g.drawRect(key, 1.5f);
        }
        if (((note % 12) + 12) % 12 == 0 && rowHeight >= 9.0f)
        {
            g.setColour(juce::Colour(0xFF3A3F4Cu));
            g.drawText(noteName(static_cast<float>(note)), key.withTrimmedRight(3.0f),
                       juce::Justification::centredRight);
        }
    }
    g.setColour(juce::Colour(0xFF2A2E38u));
    g.drawVerticalLine(static_cast<int>(kKeysWidth) - 1, keys.getY(), keys.getBottom());
}

void KaraokeView::drawSyllableLabels(juce::Graphics &g, float barHeight) const
{
    const auto &lyrics = processor.getLyrics();
    const auto &reference = processor.getReference();
    if (!lyrics.isTimed() || !reference.isLoaded())
        return;

    const double start = processor.getOptions().referenceStartSeconds;
    const double from = visibleStartSeconds() - start;
    const double to = visibleEndSeconds() - start;
    const float transpose = static_cast<float>(processor.getTranspose());
    const auto font = AppFont::getFont(juce::jlimit(10.0f, 13.0f, barHeight * 0.9f));
    g.setFont(font);

    // Syllables sharing a note are written together on it.
    int labelNote = -1;
    juce::String labelText;
    float lastRight = std::numeric_limits<float>::lowest();
    auto flush = [&]() {
        if (labelNote < 0 || labelNote >= static_cast<int>(reference.notes.size()) ||
            labelText.isEmpty())
            return;
        const auto &note = reference.notes[static_cast<size_t>(labelNote)];
        const float x = timeToX(note.startSeconds + start) + 1.0f;
        if (x < lastRight + 4.0f)
            return; // too crowded: the line below has it anyway
        const float width = juce::GlyphArrangement::getStringWidth(font, labelText) + 2.0f;
        const float top = midiToY(note.midi + transpose) - 0.5f * barHeight - font.getHeight() - 1.0f;
        const bool past = note.endSeconds + start < viewSeconds;
        const bool current = !past && note.startSeconds + start <= viewSeconds;
        g.setColour(juce::Colours::white.withAlpha(current ? 0.95f : past ? 0.3f : 0.7f));
        g.drawText(labelText, juce::Rectangle<float>(x, top, width, font.getHeight()),
                   juce::Justification::centredLeft, false);
        lastRight = x + width;
    };

    for (const auto &line : lyrics.lines)
    {
        if (!line.isTimed() || line.endSeconds < from || line.startSeconds > to)
            continue;
        for (int i = 0; i < static_cast<int>(line.syllables.size()); ++i)
        {
            const int note = line.syllables[static_cast<size_t>(i)].note;
            if (note < 0)
                continue;
            const auto text = line.syllableText(i)
                                  .trimCharactersAtStart(",.!?;:\"()[]-")
                                  .trimCharactersAtEnd(",.!?;:\"()[]-");
            if (note == labelNote)
            {
                labelText += text;
                continue;
            }
            flush();
            labelNote = note;
            labelText = text;
        }
    }
    flush();
}

void KaraokeView::drawSectionLabels(juce::Graphics &g) const
{
    const auto &lyrics = processor.getLyrics();
    const auto roll = rollArea();
    const double start = processor.getOptions().referenceStartSeconds;
    g.setFont(AppFont::getBoldFont(15.0f));
    g.setColour(juce::Colours::white.withAlpha(0.32f));
    for (const auto &line : lyrics.lines)
    {
        if (line.section.isEmpty() || !line.isTimed())
            continue;
        const float x = timeToX(line.startSeconds + start);
        if (x > roll.getRight() || x < roll.getX() - 240.0f)
            continue;
        g.drawText(line.section, juce::Rectangle<float>(x + 4.0f, roll.getY() + 8.0f, 240.0f, 20.0f),
                   juce::Justification::centredLeft, true);
    }
}

void KaraokeView::drawLyrics(juce::Graphics &g) const
{
    const auto area = lyricsArea();
    if (area.isEmpty())
        return;
    g.setColour(kLyricsBackground);
    g.fillRect(area);
    g.setColour(juce::Colour(0xFF262A34u));
    g.drawHorizontalLine(static_cast<int>(area.getY()), area.getX(), area.getRight());

    const auto &lyrics = processor.getLyrics();
    const double now = viewSeconds - processor.getOptions().referenceStartSeconds;
    const int current = lyrics.currentLineAt(now);
    if (current < 0)
        return;
    const auto &line = lyrics.lines[static_cast<size_t>(current)];

    auto content = area.reduced(20.0f, area.getHeight() * 0.08f);
    const auto mainRow = content.removeFromTop(content.getHeight() * 0.6f);
    const float left = drawLyricLine(g, line, now, mainRow, mainRow.getHeight() * 0.72f, true);

    const int next = lyrics.nextTimedLine(current);
    if (next >= 0)
        drawLyricLine(g, lyrics.lines[static_cast<size_t>(next)], now, content,
                      content.getHeight() * 0.68f, false);

    // After a long break, count the singer in: one dot per second to go.
    const int previous = lyrics.previousTimedLine(current);
    const double breakStart = previous >= 0 ? lyrics.lines[static_cast<size_t>(previous)].endSeconds
                                            : -std::numeric_limits<double>::infinity();
    const double toGo = line.startSeconds - now;
    if (toGo > 0.0 && toGo <= 3.0 && line.startSeconds - breakStart >= 4.0)
    {
        const int lit = static_cast<int>(std::ceil(toGo));
        const float size = juce::jlimit(6.0f, 11.0f, mainRow.getHeight() * 0.14f);
        const float y = mainRow.getCentreY();
        for (int i = 0; i < 3; ++i)
        {
            const float x = left - 14.0f - (3 - i) * (size + 7.0f);
            g.setColour(i >= 3 - lit ? APP_COLOR_PRIMARY : juce::Colours::white.withAlpha(0.12f));
            g.fillEllipse(x, y - 0.5f * size, size, size);
        }
    }
}

float KaraokeView::drawLyricLine(juce::Graphics &g, const KaraokeLyrics::Line &line,
                                 double referenceSeconds, juce::Rectangle<float> row,
                                 float fontHeight, bool current) const
{
    auto font = AppFont::getBoldFont(fontHeight);
    float width = juce::GlyphArrangement::getStringWidth(font, line.text);
    if (width > row.getWidth() && width > 0.0f)
    {
        font = AppFont::getBoldFont(std::max(11.0f, fontHeight * row.getWidth() / width));
        width = juce::GlyphArrangement::getStringWidth(font, line.text);
    }
    const float x = row.getCentreX() - 0.5f * width;
    const float baseline = row.getCentreY() + 0.5f * (font.getAscent() - font.getDescent());
    g.setFont(font);
    g.setColour(current ? kLyricsUnsung : kLyricsNext);
    g.drawSingleLineText(line.text, juce::roundToInt(x), juce::roundToInt(baseline));
    if (!current)
        return x;

    // Fill what has been sung: whole syllables, and the one being sung
    // part of the way, at the pace it is sung.
    int sung = -1;
    for (int i = 0; i < static_cast<int>(line.syllables.size()); ++i)
    {
        const auto &syllable = line.syllables[static_cast<size_t>(i)];
        if (syllable.isTimed() && syllable.startSeconds <= referenceSeconds)
            sung = i;
    }
    if (sung < 0)
        return x;
    const auto &syllable = line.syllables[static_cast<size_t>(sung)];
    auto widthTo = [&](int character) {
        return juce::GlyphArrangement::getStringWidth(font, line.text.substring(0, character));
    };
    const double fraction = juce::jlimit(
        0.0, 1.0,
        (referenceSeconds - syllable.startSeconds) / std::max(1.0e-3, syllable.endSeconds - syllable.startSeconds));
    const float from = widthTo(syllable.charStart);
    const float filled = from + static_cast<float>(fraction) * (widthTo(syllable.charEnd) - from);
    if (filled <= 0.0f)
        return x;

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(juce::Rectangle<float>(x - 6.0f, row.getY(), filled + 6.0f, row.getHeight())
                           .toNearestInt());
    g.setColour(juce::Colours::white);
    g.drawSingleLineText(line.text, juce::roundToInt(x), juce::roundToInt(baseline));
    return x;
}

void KaraokeView::drawOverlay(juce::Graphics &g) const
{
    const auto roll = rollArea();
    auto drawCentredBlock = [&](const juce::String &title, const juce::String &body,
                                juce::Colour bodyColour) {
        auto area = roll.withSizeKeepingCentre(std::min(roll.getWidth() - 40.0f, 520.0f), 120.0f);
        g.setColour(juce::Colour(0xCC101217u));
        g.fillRoundedRectangle(area, 12.0f);
        area.reduce(20.0f, 16.0f);
        g.setColour(juce::Colours::white);
        g.setFont(AppFont::getFont(19.0f));
        g.drawText(title, area.removeFromTop(30.0f), juce::Justification::centred);
        g.setColour(bodyColour);
        g.setFont(AppFont::getFont(13.5f));
        g.drawFittedText(body, area.toNearestInt(), juce::Justification::centred, 3);
    };

    if (processor.isAnalysing())
    {
        g.setColour(juce::Colour(0x99000000u));
        g.fillRect(roll);
        auto area = roll.withSizeKeepingCentre(std::min(roll.getWidth() - 40.0f, 420.0f), 90.0f);
        g.setColour(juce::Colours::white);
        g.setFont(AppFont::getFont(16.0f));
        g.drawText("Finding the notes of the reference...", area.removeFromTop(28.0f),
                   juce::Justification::centred);
        auto bar = area.removeFromTop(10.0f).reduced(20.0f, 0.0f);
        g.setColour(juce::Colour(0xFF2A2E38u));
        g.fillRoundedRectangle(bar, 5.0f);
        g.setColour(APP_COLOR_PRIMARY);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() *
                                             static_cast<float>(juce::jlimit(0.0, 1.0, processor.getAnalysisProgress()))),
                               5.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(AppFont::getFont(12.5f));
        g.drawText(processor.getAnalysisMessage(), area.withTrimmedTop(8.0f),
                   juce::Justification::centredTop);
    }
    else if (!processor.getReference().isLoaded())
    {
        const auto error = processor.getLastError();
        drawCentredBlock(
            "Drop the song's vocal here",
            error.isNotEmpty()
                ? error
                : "Use an isolated vocal of the song you are singing. PitchGuide finds its "
                  "notes and shows them as the song plays.",
            error.isNotEmpty() ? kOff : juce::Colours::white.withAlpha(0.65f));
    }

    if (!processor.hasSeenAudio())
    {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(AppFont::getFont(12.5f));
        g.drawText("No audio yet: put PitchGuide Live on the microphone track and turn on "
                   "input monitoring.",
                   roll.reduced(12.0f).removeFromBottom(18.0f), juce::Justification::centredLeft);
    }

    if (dropHighlight)
    {
        g.setColour(APP_COLOR_PRIMARY.withAlpha(0.15f));
        g.fillRect(getLocalBounds());
        g.setColour(APP_COLOR_PRIMARY);
        g.drawRect(getLocalBounds(), 2);
    }
}
