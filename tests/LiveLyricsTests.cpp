// PitchGuide Live: lyrics parsing (LRC, enhanced LRC, plain text), syllable
// splitting, and timing the syllables on the reference's notes.

#include "../Source/Audio/Live/KaraokeLyrics.h"
#include "../Source/Audio/Live/KaraokeReference.h"
#include "TestAssert.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace
{
bool near(double a, double b, double tolerance = 1.0e-6) { return std::abs(a - b) <= tolerance; }

int syllableCount(const char *word, bool english = true)
{
    return static_cast<int>(KaraokeLyrics::splitSyllables(juce::String::fromUTF8(word), english).size());
}

juce::String syllables(const char *word, bool english = true)
{
    const auto text = juce::String::fromUTF8(word);
    juce::StringArray parts;
    for (const auto &range : KaraokeLyrics::splitSyllables(text, english))
        parts.add(text.substring(range.getStart(), range.getEnd()));
    return parts.joinIntoString("-");
}

struct NoteSpec
{
    double start, end;
    float midi;
};

KaraokeReference referenceWith(std::initializer_list<NoteSpec> specs)
{
    KaraokeReference reference;
    reference.sourcePath = "test.wav";
    for (const auto &spec : specs)
        reference.notes.push_back({spec.start, spec.end, spec.midi});
    return reference;
}

/** A phrase of `count` legato-ish notes of `length` seconds from `start`. */
void addPhrase(KaraokeReference &reference, double start, int count, double length = 0.3,
               double gap = 0.04)
{
    for (int i = 0; i < count; ++i)
    {
        const double s = start + i * (length + gap);
        reference.notes.push_back({s, s + length, 60.0f + static_cast<float>(i % 5)});
    }
}

void testSyllables()
{
    CHECK(syllables("losing") == "lo-sing");
    CHECK(syllables("control.") == "con-trol.");
    CHECK(syllables("here") == "here");
    CHECK(syllables("go,") == "go,");
    CHECK(syllables("table") == "ta-ble");
    CHECK(syllables("picking") == "pick-ing");
    CHECK(syllables("going") == "go-ing");
    CHECK(syllableCount("loved") == 1);
    CHECK(syllableCount("wanted") == 2);
    CHECK(syllableCount("comes") == 1);
    CHECK(syllableCount("places") == 2);
    CHECK(syllableCount("while") == 1);
    CHECK(syllableCount("I'm") == 1);
    CHECK(syllableCount("you") == 1);
    CHECK(syllableCount("player") == 2);
    CHECK(syllableCount("beautiful") == 3);
    CHECK(syllableCount("...") == 0);
    // Syllabic r/l and other languages: no vowels still sings as one.
    CHECK(syllableCount("vlk", false) == 1);
    CHECK(syllables("m\xc3\xa1me", false) == juce::String::fromUTF8("m\xc3\xa1-me"));
    CHECK(syllableCount("m\xc3\xa1me", false) == 2);

    // Ranges cover the word with nothing lost.
    const auto word = juce::String("\"beautiful,\"");
    const auto ranges = KaraokeLyrics::splitSyllables(word, true);
    CHECK(!ranges.empty() && ranges.front().getStart() == 0 &&
          ranges.back().getEnd() == word.length());
    for (size_t i = 1; i < ranges.size(); ++i)
        CHECK(ranges[i].getStart() == ranges[i - 1].getEnd());
}

void testParseLrc()
{
    const auto lyrics = KaraokeLyrics::parse(
        juce::String::fromUTF8("\xef\xbb\xbf[ar:Someone]\r\n"
                               "[ti:A Song]\n"
                               "[offset:+250]\n"
                               "[00:20.50]Second   line here\n"
                               "[00:12.00][01:30.00]And here I go, losing my control.\n"
                               "[00:25.00]\n"
                               "[00:30.10]After the break\n"
                               "\n"
                               "[00:33.5]Short stamp\n"));
    CHECK(lyrics.sourceTiming == KaraokeLyrics::SourceTiming::Lines);
    CHECK(near(lyrics.fileOffsetSeconds, 0.25));
    CHECK(lyrics.lines.size() == 5);
    CHECK(near(lyrics.lines[0].lrcSeconds, 12.0));
    CHECK(lyrics.lines[0].text == "And here I go, losing my control.");
    CHECK(near(lyrics.lines[0].lrcEndSeconds, 20.5));
    CHECK(lyrics.lines[1].text == "Second line here");
    // The blank line is an instrumental break: line 2 ends there.
    CHECK(near(lyrics.lines[1].lrcEndSeconds, 25.0));
    CHECK(near(lyrics.lines[2].lrcSeconds, 30.1));
    CHECK(near(lyrics.lines[3].lrcSeconds, 33.5));
    CHECK(near(lyrics.lines[4].lrcSeconds, 90.0));
    CHECK(lyrics.lines[4].text == lyrics.lines[0].text);
    CHECK(lyrics.lines[4].lrcEndSeconds < 0.0);

    const auto &line = lyrics.lines[0];
    CHECK(line.words.size() == 7);
    // And here I go, lo-sing my con-trol.
    CHECK(line.syllables.size() == 9);
    CHECK(line.syllableText(4) == "lo");
    CHECK(line.syllableText(5) == "sing");
    CHECK(line.syllableText(8) == "trol.");

    // Old-style hundredths and the mm:ss form parse; brackets in the sung
    // text stay text.
    const auto other = KaraokeLyrics::parse("[01:02:50]one\n[00:03]two [x]\n");
    CHECK(other.lines.size() == 2);
    CHECK(near(other.lines[0].lrcSeconds, 3.0));
    CHECK(other.lines[0].text == "two [x]");
    CHECK(near(other.lines[1].lrcSeconds, 62.5));
}

void testParseEnhancedAndPlain()
{
    const auto enhanced = KaraokeLyrics::parse(
        "[00:10.00]<00:10.00> And <00:10.40> here <00:10.80> I <00:11.00> go, "
        "<00:11.50> lo<00:11.80>sing <00:12.30>\n"
        "[00:14.00]<00:14.20>my <00:14.50>con<00:14.90>trol\n");
    CHECK(enhanced.sourceTiming == KaraokeLyrics::SourceTiming::Words);
    CHECK(enhanced.lines.size() == 2);
    const auto &first = enhanced.lines[0];
    CHECK(first.text == "And here I go, losing");
    CHECK(first.syllables.size() == 6);
    CHECK(near(first.syllables[0].lrcSeconds, 10.0));
    CHECK(near(first.syllables[3].lrcSeconds, 11.0));
    CHECK(first.syllableText(4) == "lo");
    CHECK(near(first.syllables[4].lrcSeconds, 11.5));
    CHECK(first.syllableText(5) == "sing");
    CHECK(near(first.syllables[5].lrcSeconds, 11.8));
    // The time after the last word ends the line.
    CHECK(near(first.lrcEndSeconds, 12.3));
    CHECK(enhanced.lines[1].syllableText(1) == "con");
    CHECK(near(enhanced.lines[1].syllables[2].lrcSeconds, 14.9));

    const auto plain = KaraokeLyrics::parse("[Verse 1]\n"
                                            "And here I go\n"
                                            "\n"
                                            "[Chorus: Someone]\n"
                                            "Losing my control\n");
    CHECK(plain.sourceTiming == KaraokeLyrics::SourceTiming::None);
    CHECK(plain.lines.size() == 2);
    CHECK(plain.lines[0].section == "Verse 1");
    CHECK(plain.lines[1].section == "Chorus");
    CHECK(plain.lines[1].text == "Losing my control");
    CHECK(!plain.isTimed());
}

void testLrcTimingOnNotes()
{
    // "And here I go, losing my control." = 9 syllables. The reference sings
    // it from 12.3 s with one note per syllable, except "trol" held over
    // two notes that move pitch without a pause.
    KaraokeReference reference;
    addPhrase(reference, 12.3, 8);
    const double last = reference.notes.back().endSeconds;
    reference.notes.push_back({last + 0.05, last + 0.5, 64.0f});
    reference.notes.push_back({last + 0.5, last + 1.2, 62.0f});
    // An ad-lib long after the line, before the next one.
    reference.notes.push_back({19.0, 19.4, 60.0f});
    addPhrase(reference, 20.6, 4);

    auto lyrics = KaraokeLyrics::parse("[00:12.00]And here I go, losing my control.\n"
                                       "[00:20.50]Second line here\n");
    lyrics.applyTiming(&reference, 0.0);

    const auto &line = lyrics.lines[0];
    CHECK(line.isTimed());
    // Snapped from the written 12.0 to the phrase the reference sings.
    CHECK(near(line.startSeconds, 12.3));
    for (size_t i = 0; i < 8; ++i)
    {
        CHECK(line.syllables[i].note == static_cast<int>(i));
        CHECK(near(line.syllables[i].startSeconds, reference.notes[i].startSeconds));
    }
    CHECK(line.syllables[8].note == 8);
    CHECK(near(line.syllables[8].endSeconds, last + 1.2));
    CHECK(near(line.endSeconds, last + 1.2));

    const auto &second = lyrics.lines[1];
    CHECK(second.syllables.size() == 4);
    CHECK(near(second.startSeconds, 20.6));
    CHECK(second.syllables[0].note == 11);

    // Fewer notes than syllables: the longest note takes two.
    auto squeezed = referenceWith({{5.0, 5.3, 60}, {5.35, 6.6, 62}});
    auto three = KaraokeLyrics::parse("[00:05.00]one two three\n");
    three.applyTiming(&squeezed, 0.0);
    const auto &s = three.lines[0].syllables;
    CHECK(s.size() == 3);
    CHECK(s[0].note == 0 && s[1].note == 1 && s[2].note == 1);
    CHECK(near(s[1].startSeconds, 5.35) && near(s[2].endSeconds, 6.6));
    CHECK(s[2].startSeconds > s[1].startSeconds);

    // More notes than syllables: boundaries go to the pauses.
    auto melisma = referenceWith({{1.0, 1.3, 60}, {1.3, 1.6, 62}, {1.6, 1.9, 64}, {2.2, 2.5, 60}});
    auto two = KaraokeLyrics::parse("[00:01.00]oh yeah\n");
    two.applyTiming(&melisma, 0.0);
    CHECK(two.lines[0].syllables[0].note == 0);
    CHECK(near(two.lines[0].syllables[0].endSeconds, 1.9));
    CHECK(two.lines[0].syllables[1].note == 3);

    // No reference: lines keep their written times and the syllables are spread.
    auto alone = KaraokeLyrics::parse("[00:12.00]And here I go\n[00:20.50]Next\n");
    alone.applyTiming(nullptr, 1.0);
    CHECK(alone.isTimed());
    CHECK(near(alone.lines[0].startSeconds, 13.0));
    CHECK(alone.lines[0].endSeconds < 21.5);
    CHECK(alone.lines[0].syllables[0].note == -1);
}

void testEnhancedTimingOnNotes()
{
    auto reference = referenceWith({{10.05, 10.35, 60}, {10.42, 10.75, 62}, {10.82, 11.0, 64},
                                    {11.05, 11.45, 62}, {11.52, 11.75, 60}, {11.82, 12.25, 59}});
    auto lyrics = KaraokeLyrics::parse(
        "[00:10.00]<00:10.00> And <00:10.40> here <00:10.80> I <00:11.00> go, "
        "<00:11.50> lo<00:11.80>sing <00:12.30>\n");
    lyrics.applyTiming(&reference, 0.0);
    const auto &line = lyrics.lines[0];
    for (int i = 0; i < 6; ++i)
        CHECK(line.syllables[static_cast<size_t>(i)].note == i);
    CHECK(near(line.endSeconds, 12.25));
}

void testPlainTextOnPhrases()
{
    // Three phrases with clear breaths; 4, 3 and 5 syllables.
    KaraokeReference reference;
    reference.notes.push_back({0.5, 0.8, 55.0f}); // an intro ad-lib
    addPhrase(reference, 3.0, 4);
    addPhrase(reference, 5.5, 3);
    addPhrase(reference, 8.0, 5);
    auto lyrics = KaraokeLyrics::parse("[Verse]\nAnd here I go\nlosing my\ncontrol it again\n");
    lyrics.applyTiming(&reference, 0.0);
    CHECK(lyrics.numTimedLines() == 3);
    CHECK(near(lyrics.lines[0].startSeconds, 3.0));
    CHECK(near(lyrics.lines[1].startSeconds, 5.5));
    CHECK(near(lyrics.lines[2].startSeconds, 8.0));
    CHECK(lyrics.lines[2].syllables.size() == 5);
    CHECK(lyrics.lines[2].syllables[4].note == static_cast<int>(reference.notes.size()) - 1);

    // Without a reference plain text cannot be timed.
    lyrics.applyTiming(nullptr, 0.0);
    CHECK(!lyrics.isTimed());
}

void testEstimateOffset()
{
    KaraokeReference reference;
    const double phraseStarts[] = {4.0, 7.3, 11.1, 13.9, 18.4, 21.0, 25.6, 29.2};
    for (const double start : phraseStarts)
        addPhrase(reference, start, 5);

    juce::String text;
    for (size_t i = 0; i < std::size(phraseStarts); ++i)
    {
        // The file is 2.3 s late, with some hand-tapped jitter.
        const double written = phraseStarts[i] + 2.3 + (static_cast<int>(i % 3) - 1) * 0.06;
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "[%02d:%05.2f]", static_cast<int>(written / 60.0),
                      std::fmod(written, 60.0));
        text << stamp << "la la la la la\n";
    }
    auto lyrics = KaraokeLyrics::parse(text);
    CHECK(lyrics.lines.size() == std::size(phraseStarts));
    const auto offset = lyrics.estimateOffset(reference, true);
    CHECK(offset.has_value());
    CHECK(near(*offset, -2.3, 0.08));

    lyrics.applyTiming(&reference, *offset);
    for (size_t i = 0; i < std::size(phraseStarts); ++i)
        CHECK(near(lyrics.lines[i].startSeconds, phraseStarts[i]));

    // A file that is already right is left alone.
    auto right = KaraokeLyrics::parse("[00:04.00]la la la la la\n[00:07.30]la la la la la\n"
                                      "[00:11.10]la la la la la\n[00:13.90]la la la la la\n");
    CHECK(!right.estimateOffset(reference, true).has_value());
    CHECK(!KaraokeLyrics::parse("plain text").estimateOffset(reference, false).has_value());
}

void testCurrentLine()
{
    auto reference = referenceWith({});
    addPhrase(reference, 2.0, 3);  // ends 2.0 + 3*0.34 - 0.04 = 2.98
    addPhrase(reference, 10.0, 3); // ends 10.98
    auto lyrics = KaraokeLyrics::parse("[00:02.00]one two three\n[00:10.00]four five six\n");
    lyrics.applyTiming(&reference, 0.0);
    CHECK(lyrics.currentLineAt(0.0) == 0);   // read ahead before the song
    CHECK(lyrics.currentLineAt(2.5) == 0);
    CHECK(lyrics.currentLineAt(3.2) == 0);   // held a moment after the line ends
    CHECK(lyrics.currentLineAt(5.0) == 1);   // then the next one shows
    CHECK(lyrics.currentLineAt(30.0) == 1);  // the last one stays
    CHECK(lyrics.nextTimedLine(0) == 1);
    CHECK(lyrics.nextTimedLine(1) == -1);
    CHECK(lyrics.previousTimedLine(1) == 0);
    CHECK(KaraokeLyrics().currentLineAt(1.0) == -1);
}
} // namespace

int main()
{
    testSyllables();
    testParseLrc();
    testParseEnhancedAndPlain();
    testLrcTimingOnNotes();
    testEnhancedTimingOnNotes();
    testPlainTextOnPhrases();
    testEstimateOffset();
    testCurrentLine();
    std::printf("LiveLyricsTests passed\n");
    return 0;
}
