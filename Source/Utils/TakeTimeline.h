#pragma once

#include "../Models/Note.h"
#include <vector>

/**
 * Where the take, as sung, sits on the song timeline after timing edits.
 *
 * Maps a song (output) frame to the take frame (source frame) sung there,
 * through the notes' source and visual output ranges: linearly inside a note,
 * from one note's end to the next one's start between notes, and with the
 * nearest note's offset before the first or after the last. Without timing
 * edits it is the identity.
 */
class TakeTimeline
{
public:
    explicit TakeTimeline(const std::vector<Note> &notes);

    float sourceFrameAt(float outputFrame) const;

private:
    struct Span
    {
        float outStart;
        float outEnd;
        float srcStart;
        float srcEnd;
    };
    std::vector<Span> spans; // sorted, with non-empty output ranges
};
