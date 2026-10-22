#include "TakeTimeline.h"

#include <algorithm>

TakeTimeline::TakeTimeline(const std::vector<Note> &notes)
{
    for (const auto &note : notes)
        if (!note.isRest() && note.getVisualEndFrame() > note.getVisualStartFrame())
            spans.push_back({note.getVisualStartFrame(), note.getVisualEndFrame(),
                             static_cast<float>(note.getSrcStartFrame()),
                             static_cast<float>(note.getSrcEndFrame())});
    std::sort(spans.begin(), spans.end(),
              [](const Span &a, const Span &b) { return a.outStart < b.outStart; });
}

float TakeTimeline::sourceFrameAt(float outputFrame) const
{
    if (spans.empty())
        return outputFrame;

    // The first span that ends after the frame.
    const auto next = std::upper_bound(spans.begin(), spans.end(), outputFrame,
                                       [](float frame, const Span &span) { return frame < span.outEnd; });
    if (next == spans.end())
        return outputFrame + (spans.back().srcEnd - spans.back().outEnd);
    if (outputFrame >= next->outStart)
    {
        const float t = (outputFrame - next->outStart) / (next->outEnd - next->outStart);
        return next->srcStart + t * (next->srcEnd - next->srcStart);
    }
    if (next == spans.begin())
        return outputFrame + (next->srcStart - next->outStart);

    const auto &previous = *(next - 1);
    const float gap = next->outStart - previous.outEnd;
    if (gap <= 0.0f)
        return next->srcStart;
    const float t = (outputFrame - previous.outEnd) / gap;
    return previous.srcEnd + t * (next->srcStart - previous.srcEnd);
}
