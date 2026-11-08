#include "KaraokeLyrics.h"

#include "KaraokeReference.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace
{
using Notes = std::vector<KaraokeReference::Note>;

// ---- Text ------------------------------------------------------------------

std::u32string toU32(const juce::String &text)
{
    std::u32string out;
    for (auto p = text.getCharPointer(); !p.isEmpty();)
        out.push_back(static_cast<char32_t>(p.getAndAdvance()));
    return out;
}

juce::String fromU32(const std::u32string &text)
{
    juce::String out;
    out.preallocateBytes(text.size() + 4);
    for (const auto c : text)
        out += juce::String::charToString(static_cast<juce::juce_wchar>(c));
    return out;
}

bool isLetter(char32_t c)
{
    return juce::CharacterFunctions::isLetter(static_cast<juce::juce_wchar>(c));
}

bool isSpace(char32_t c)
{
    return juce::CharacterFunctions::isWhitespace(static_cast<juce::juce_wchar>(c));
}

char32_t lower(char32_t c)
{
    return static_cast<char32_t>(
        juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(c)));
}

bool isVowelLetter(char32_t c)
{
    // a e i o u y and their accented forms (Czech, German, French, Nordic, Baltic...).
    static const std::u32string vowels = [] {
        const char32_t codes[] = {
            0x61, 0x65, 0x69, 0x6f, 0x75, 0x79, 0xe1, 0xe9, 0xed, 0xf3, 0xfa, 0x16f,
            0xfd, 0x11b, 0xe4, 0xeb, 0xef, 0xf6, 0xfc, 0xe0, 0xe8, 0xec, 0xf2, 0xf9,
            0xe2, 0xea, 0xee, 0xf4, 0xfb, 0xe3, 0xf5, 0xe5, 0xe6, 0xf8, 0x153, 0x105,
            0x119, 0x12f, 0x173, 0x117, 0x16b, 0x101, 0x113, 0x12b, 0x14d};
        return std::u32string(std::begin(codes), std::end(codes));
    }();
    return vowels.find(lower(c)) != std::u32string::npos;
}

/** "mm:ss", "mm:ss.xx", "mm:ss.xxx" or the old "mm:ss:xx". */
bool parseTimestamp(const juce::String &raw, double &seconds)
{
    const auto text = raw.trim();
    if (text.isEmpty() || !juce::CharacterFunctions::isDigit(text[0]) ||
        !text.containsOnly("0123456789:."))
        return false;
    juce::StringArray parts;
    parts.addTokens(text, ":", "");
    for (const auto &part : parts)
        if (part.isEmpty())
            return false;
    if (parts.size() == 2)
        seconds = parts[0].getIntValue() * 60.0 + parts[1].getDoubleValue();
    else if (parts.size() == 3 && !parts[2].containsChar('.'))
        seconds = parts[0].getIntValue() * 60.0 + parts[1].getIntValue() +
                  parts[2].getIntValue() / std::pow(10.0, parts[2].length());
    else
        return false;
    return std::isfinite(seconds) && seconds >= 0.0;
}

bool isMetadataKey(const juce::String &key)
{
    static const juce::StringArray keys{"ar", "ti", "al", "au", "by", "length", "offset",
                                        "re", "ve", "tool", "la", "lang", "id", "#"};
    return keys.contains(key);
}

/** Text with <mm:ss.xx> word times taken out, and where they were. */
struct CleanLine
{
    std::u32string text;
    std::vector<std::pair<int, double>> stamps;
};

CleanLine cleanLine(const juce::String &body)
{
    CleanLine out;
    const auto source = toU32(body);
    for (size_t i = 0; i < source.size(); ++i)
    {
        const char32_t c = source[i];
        if (c == U'<')
        {
            const auto close = source.find(U'>', i);
            double seconds = 0.0;
            if (close != std::u32string::npos &&
                parseTimestamp(fromU32(source.substr(i + 1, close - i - 1)), seconds))
            {
                out.stamps.emplace_back(static_cast<int>(out.text.size()), seconds);
                i = close;
                continue;
            }
        }
        if (isSpace(c))
        {
            if (!out.text.empty() && out.text.back() != U' ')
                out.text.push_back(U' ');
            continue;
        }
        out.text.push_back(c);
    }
    while (!out.text.empty() && out.text.back() == U' ')
        out.text.pop_back();
    // A word time written after a space belongs to the word that follows it.
    for (auto &stamp : out.stamps)
    {
        stamp.first = std::min(stamp.first, static_cast<int>(out.text.size()));
        if (stamp.first < static_cast<int>(out.text.size()) &&
            out.text[static_cast<size_t>(stamp.first)] == U' ')
            ++stamp.first;
    }
    return out;
}

/** Words and syllables of a cleaned line. Returns the time written after the
    last word (the end of the line), or -1. */
double buildWords(KaraokeLyrics::Line &line, const CleanLine &clean, bool englishRules)
{
    const auto &text = clean.text;
    const int length = static_cast<int>(text.size());
    line.text = fromU32(text);

    for (int start = 0; start < length;)
    {
        if (text[static_cast<size_t>(start)] == U' ')
        {
            ++start;
            continue;
        }
        int end = start;
        while (end < length && text[static_cast<size_t>(end)] != U' ')
            ++end;

        KaraokeLyrics::Word word;
        word.charStart = start;
        word.charEnd = end;
        word.firstSyllable = static_cast<int>(line.syllables.size());

        // Times written inside a word are its syllable boundaries; otherwise
        // the word is split by its vowels.
        std::vector<int> inside;
        for (const auto &stamp : clean.stamps)
            if (stamp.first > start && stamp.first < end)
                inside.push_back(stamp.first);
        std::sort(inside.begin(), inside.end());
        inside.erase(std::unique(inside.begin(), inside.end()), inside.end());

        bool hasLetters = false;
        for (int i = start; i < end; ++i)
            hasLetters = hasLetters || isLetter(text[static_cast<size_t>(i)]);

        if (hasLetters && !inside.empty())
        {
            int from = start;
            for (const int boundary : inside)
            {
                line.syllables.push_back({from, boundary});
                from = boundary;
            }
            line.syllables.push_back({from, end});
        }
        else if (hasLetters)
        {
            const auto ranges = KaraokeLyrics::splitSyllables(
                fromU32(text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start))),
                englishRules);
            for (const auto &range : ranges)
                line.syllables.push_back({start + range.getStart(), start + range.getEnd()});
        }
        word.numSyllables = static_cast<int>(line.syllables.size()) - word.firstSyllable;
        line.words.push_back(word);
        start = end;
    }

    double endTime = -1.0;
    for (const auto &stamp : clean.stamps)
    {
        if (stamp.first >= length)
        {
            endTime = stamp.second;
            continue;
        }
        for (auto &syllable : line.syllables)
            if (syllable.charStart >= stamp.first)
            {
                if (syllable.lrcSeconds < 0.0)
                    syllable.lrcSeconds = stamp.second;
                break;
            }
    }
    return endTime;
}

// ---- Timing ----------------------------------------------------------------

double gapBefore(const Notes &notes, size_t index)
{
    return index == 0 ? 10.0 : notes[index].startSeconds - notes[index - 1].endSeconds;
}

size_t firstNoteStartingAtOrAfter(const Notes &notes, double seconds, size_t from)
{
    const auto it = std::lower_bound(
        notes.begin() + static_cast<std::ptrdiff_t>(std::min(from, notes.size())), notes.end(),
        seconds, [](const KaraokeReference::Note &note, double t) { return note.startSeconds < t; });
    return static_cast<size_t>(std::distance(notes.begin(), it));
}

/**
 * The note a phrase written at `seconds` starts on: among the notes starting
 * within [seconds - before, seconds + after], the one after the longest
 * pause, nudged towards the written time. Hand-made LRC times are often a
 * little early or late; the breath before the phrase is not.
 */
size_t snapToNote(const Notes &notes, double seconds, double before, double after, size_t from)
{
    const size_t lo = firstNoteStartingAtOrAfter(notes, seconds - before, from);
    const size_t hi = firstNoteStartingAtOrAfter(notes, seconds + after, lo);
    size_t best = notes.size();
    double bestScore = -std::numeric_limits<double>::infinity();
    for (size_t i = lo; i < hi; ++i)
    {
        const double score = std::min(gapBefore(notes, i), 0.6) -
                             0.5 * std::abs(notes[i].startSeconds - (seconds + 0.05));
        if (score > bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    if (best < notes.size())
        return best;
    return firstNoteStartingAtOrAfter(notes, seconds, from);
}

void spreadSyllables(KaraokeLyrics::Line &line, int first, int last, double from, double to)
{
    const int count = last - first;
    if (count <= 0)
        return;
    if (to <= from + 0.05)
        to = from + 0.25 * count;
    const double step = (to - from) / count;
    for (int i = 0; i < count; ++i)
    {
        auto &syllable = line.syllables[static_cast<size_t>(first + i)];
        syllable.startSeconds = from + i * step;
        syllable.endSeconds = from + (i + 1) * step;
        syllable.note = -1;
    }
}

/**
 * Puts syllables [first, last) of a line on notes [n0, n1).
 *
 * More notes than syllables: every syllable gets a run of notes, and the
 * syllable boundaries go where the pauses between notes are (a new syllable
 * usually starts after a consonant gap; a held vowel moving pitch does not
 * pause). Fewer notes than syllables: the longest notes are shared.
 */
void assignSyllables(KaraokeLyrics::Line &line, int first, int last, const Notes &notes,
                     size_t n0, size_t n1, double fallbackFrom, double fallbackTo)
{
    const int syllables = last - first;
    if (syllables <= 0)
        return;
    if (n1 <= n0)
    {
        spreadSyllables(line, first, last, fallbackFrom, fallbackTo);
        return;
    }
    const int count = static_cast<int>(n1 - n0);
    auto setSyllable = [&](int syllable, size_t note, double start, double end) {
        auto &s = line.syllables[static_cast<size_t>(first + syllable)];
        s.note = static_cast<int>(note);
        s.startSeconds = start;
        s.endSeconds = std::max(end, start + 0.01);
    };

    if (count >= syllables)
    {
        constexpr double kGroupPenalty = 0.12;
        const double negInf = -std::numeric_limits<double>::infinity();
        const int width = count + 1;
        std::vector<double> best(static_cast<size_t>((syllables + 1) * width), negInf);
        std::vector<int> previous(best.size(), -1);
        auto at = [width](int i, int j) { return static_cast<size_t>(i * width + j); };
        best[at(0, 0)] = 0.0;
        for (int i = 0; i < syllables; ++i)
        {
            const int remaining = syllables - i - 1;
            for (int j = i; j <= count - remaining - 1; ++j)
            {
                const double base = best[at(i, j)];
                if (base == negInf)
                    continue;
                const double boundary =
                    i == 0 ? 0.0 : std::min(gapBefore(notes, n0 + static_cast<size_t>(j)), 0.25) * 4.0;
                const int minTake = i == syllables - 1 ? count - j : 1;
                const int maxTake = count - j - remaining;
                for (int take = minTake; take <= maxTake; ++take)
                {
                    const double value = base + boundary - kGroupPenalty * (take - 1) * (take - 1);
                    if (value > best[at(i + 1, j + take)])
                    {
                        best[at(i + 1, j + take)] = value;
                        previous[at(i + 1, j + take)] = j;
                    }
                }
            }
        }
        int j = count;
        for (int i = syllables; i > 0; --i)
        {
            const int from = previous[at(i, j)];
            jassert(from >= 0);
            const size_t a = n0 + static_cast<size_t>(from);
            const size_t b = n0 + static_cast<size_t>(j) - 1;
            setSyllable(i - 1, a, notes[a].startSeconds, notes[b].endSeconds);
            j = from;
        }
        return;
    }

    // Fewer notes than syllables: each note carries at least one, and the
    // extra ones go to whichever note gives them the most time.
    std::vector<int> share(static_cast<size_t>(count), 1);
    for (int extra = syllables - count; extra > 0; --extra)
    {
        int bestNote = 0;
        double bestTime = -1.0;
        for (int j = 0; j < count; ++j)
        {
            const auto &note = notes[n0 + static_cast<size_t>(j)];
            const double time = (note.endSeconds - note.startSeconds) / (share[static_cast<size_t>(j)] + 1);
            if (time > bestTime)
            {
                bestTime = time;
                bestNote = j;
            }
        }
        ++share[static_cast<size_t>(bestNote)];
    }
    int syllable = 0;
    for (int j = 0; j < count; ++j)
    {
        const size_t index = n0 + static_cast<size_t>(j);
        const auto &note = notes[index];
        const int parts = share[static_cast<size_t>(j)];
        const double step = (note.endSeconds - note.startSeconds) / parts;
        for (int part = 0; part < parts; ++part, ++syllable)
            setSyllable(syllable, index, note.startSeconds + part * step,
                        note.startSeconds + (part + 1) * step);
    }
}

void finishLine(KaraokeLyrics::Line &line)
{
    line.startSeconds = -1.0;
    line.endSeconds = -1.0;
    for (const auto &syllable : line.syllables)
    {
        if (!syllable.isTimed())
            continue;
        if (line.startSeconds < 0.0)
            line.startSeconds = syllable.startSeconds;
        line.endSeconds = std::max(line.endSeconds, syllable.endSeconds);
    }
}
} // namespace

// ---- KaraokeLyrics -----------------------------------------------------------

juce::String KaraokeLyrics::Line::syllableText(int index) const
{
    if (index < 0 || index >= static_cast<int>(syllables.size()))
        return {};
    const auto &syllable = syllables[static_cast<size_t>(index)];
    return text.substring(syllable.charStart, syllable.charEnd);
}

bool KaraokeLyrics::isTimed() const
{
    return numTimedLines() > 0;
}

int KaraokeLyrics::numTimedLines() const
{
    return static_cast<int>(std::count_if(lines.begin(), lines.end(),
                                          [](const Line &line) { return line.isTimed(); }));
}

std::vector<juce::Range<int>> KaraokeLyrics::splitSyllables(const juce::String &wordText,
                                                            bool englishRules)
{
    const auto word = toU32(wordText);
    const int length = static_cast<int>(word.size());
    std::vector<char32_t> w(word.size());
    std::transform(word.begin(), word.end(), w.begin(), lower);

    auto letter = [&](int i) { return i >= 0 && i < length && isLetter(w[static_cast<size_t>(i)]); };
    std::vector<bool> vowel(static_cast<size_t>(length), false);
    bool anyLetter = false;
    for (int i = 0; i < length; ++i)
    {
        if (!letter(i))
            continue;
        anyLetter = true;
        const char32_t c = w[static_cast<size_t>(i)];
        bool isVowel = isVowelLetter(c);
        // "you", "player": a y before a vowel is a consonant.
        if (c == U'y' && i + 1 < length && letter(i + 1) && isVowelLetter(w[static_cast<size_t>(i + 1)]))
            isVowel = false;
        vowel[static_cast<size_t>(i)] = isVowel;
    }
    if (!anyLetter)
        return {};

    struct Group
    {
        int start, end;
    };
    std::vector<Group> groups;
    for (int i = 0; i < length;)
    {
        if (!vowel[static_cast<size_t>(i)])
        {
            ++i;
            continue;
        }
        int end = i;
        while (end < length && vowel[static_cast<size_t>(end)])
            ++end;
        groups.push_back({i, end});
        i = end;
    }
    if (englishRules)
    {
        // "going", "being": the i of -ing is its own syllable.
        for (size_t g = 0; g < groups.size(); ++g)
        {
            const int end = groups[g].end;
            if (end - groups[g].start >= 2 && w[static_cast<size_t>(end - 1)] == U'i' &&
                end + 1 < length && w[static_cast<size_t>(end)] == U'n' &&
                w[static_cast<size_t>(end + 1)] == U'g')
            {
                groups[g].end = end - 1;
                groups.insert(groups.begin() + static_cast<std::ptrdiff_t>(g + 1),
                              Group{end - 1, end});
                ++g;
            }
        }
    }
    if (groups.size() <= 1)
        return {juce::Range<int>(0, length)};

    if (englishRules)
    {
        int lastLetter = length - 1;
        while (lastLetter >= 0 && !letter(lastLetter))
            --lastLetter;
        auto at = [&](int i) { return i >= 0 && i < length ? w[static_cast<size_t>(i)] : U'\0'; };
        auto consonant = [&](int i) { return letter(i) && !vowel[static_cast<size_t>(i)]; };
        const auto &g = groups.back();
        bool silent = false;
        if (g.end - g.start == 1 && at(g.start) == U'e')
        {
            const int before = g.start - 1;
            if (g.start == lastLetter && consonant(before))
                // "here", "while" - but "table", "little" keep their -le.
                silent = !(at(before) == U'l' && consonant(before - 1));
            else if (g.start == lastLetter - 1 && at(lastLetter) == U'd' && consonant(before))
                silent = at(before) != U't' && at(before) != U'd'; // "loved", not "wanted"
            else if (g.start == lastLetter - 1 && at(lastLetter) == U's' && consonant(before))
            {
                const std::u32string keeps = U"sxzcgh";
                silent = keeps.find(at(before)) == std::u32string::npos; // "comes", not "places"
            }
        }
        if (silent)
            groups.pop_back();
        if (groups.size() <= 1)
            return {juce::Range<int>(0, length)};
    }

    auto pairIs = [&](int i, const char32_t *pair) {
        return i >= 0 && i + 1 < length && w[static_cast<size_t>(i)] == pair[0] &&
               w[static_cast<size_t>(i + 1)] == pair[1];
    };
    auto isOnsetCluster = [&](int i) {
        // consonant + l/r starts a syllable: "con-trol", "ta-ble".
        if (i < 0 || i + 1 >= length || !letter(i) || vowel[static_cast<size_t>(i)])
            return false;
        const char32_t second = w[static_cast<size_t>(i + 1)];
        const char32_t c = w[static_cast<size_t>(i)];
        return (second == U'r' || second == U'l') && c != U'r' && c != U'l';
    };

    std::vector<int> breaks;
    for (size_t g = 0; g + 1 < groups.size(); ++g)
    {
        const int from = groups[g].end;
        const int to = groups[g + 1].start;
        const int between = to - from;
        int at = from;
        if (between >= 2)
        {
            at = to - 1;
            const int pair = to - 2;
            if (pairIs(pair, U"ck"))
                at = to;
            else if (pairIs(pair, U"ch") || pairIs(pair, U"sh") || pairIs(pair, U"th") ||
                     pairIs(pair, U"ph") || pairIs(pair, U"wh") || pairIs(pair, U"gh") ||
                     isOnsetCluster(pair))
                at = pair;
        }
        at = juce::jlimit(1, length - 1, at);
        if (breaks.empty() || at > breaks.back())
            breaks.push_back(at);
    }

    std::vector<juce::Range<int>> ranges;
    int start = 0;
    for (const int b : breaks)
    {
        ranges.emplace_back(start, b);
        start = b;
    }
    ranges.emplace_back(start, length);
    return ranges;
}

KaraokeLyrics KaraokeLyrics::parse(const juce::String &text)
{
    KaraokeLyrics lyrics;

    // Silent final e only makes sense for English-looking text.
    int letters = 0;
    int nonAscii = 0;
    for (auto p = text.getCharPointer(); !p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (juce::CharacterFunctions::isLetter(c))
        {
            ++letters;
            if (c > 127)
                ++nonAscii;
        }
    }
    const bool englishRules = nonAscii * 100 < std::max(1, letters);

    struct Entry
    {
        double seconds;
        juce::String body;
        juce::String section;
    };
    std::vector<Entry> timed;
    std::vector<double> breaks;
    std::vector<std::pair<juce::String, juce::String>> plain;
    juce::String pendingSection;

    juce::StringArray rawLines;
    rawLines.addLines(text);
    for (auto line : rawLines)
    {
        line = line.trim();
        if (line.startsWithChar(static_cast<juce::juce_wchar>(0xfeff)))
            line = line.substring(1).trim();

        std::vector<double> times;
        bool consumed = false;
        while (line.startsWithChar('['))
        {
            const int close = line.indexOfChar(']');
            if (close < 0)
                break;
            const auto content = line.substring(1, close);
            const auto rest = line.substring(close + 1).trim();
            double seconds = 0.0;
            if (parseTimestamp(content, seconds))
            {
                times.push_back(seconds);
                line = rest;
                continue;
            }
            if (!times.empty() || rest.isNotEmpty())
                break; // brackets that are part of the sung text
            const int colon = content.indexOfChar(':');
            const auto key = colon > 0 ? content.substring(0, colon).trim().toLowerCase() : juce::String();
            if (isMetadataKey(key))
            {
                if (key == "offset")
                {
                    // getIntValue() does not take a leading '+'.
                    const auto value = content.substring(colon + 1).trim();
                    lyrics.fileOffsetSeconds =
                        (value.startsWithChar('+') ? value.substring(1) : value).getIntValue() / 1000.0;
                }
            }
            else
            {
                // "[Chorus]", "[Verse 2: Artist]": a section header.
                auto section = colon > 0 ? content.substring(0, colon) : content;
                pendingSection = section.trim();
            }
            consumed = true;
            line.clear();
            break;
        }
        if (consumed)
            continue;

        if (!times.empty())
        {
            const auto clean = cleanLine(line);
            const bool blank = std::none_of(clean.text.begin(), clean.text.end(), isLetter) &&
                               std::all_of(clean.text.begin(), clean.text.end(), [](char32_t c) {
                                   return isSpace(c) || c == char32_t{0x266a} || c == char32_t{0x266b} || c == U'.';
                               });
            for (const double seconds : times)
            {
                if (blank)
                    breaks.push_back(seconds);
                else
                    timed.push_back({seconds, line, pendingSection});
            }
            if (!blank)
                pendingSection.clear();
        }
        else if (line.isNotEmpty())
        {
            plain.emplace_back(line, pendingSection);
            pendingSection.clear();
        }
    }

    if (!timed.empty())
    {
        std::stable_sort(timed.begin(), timed.end(),
                         [](const Entry &a, const Entry &b) { return a.seconds < b.seconds; });
        std::sort(breaks.begin(), breaks.end());
        bool anyWordTimes = false;
        for (size_t i = 0; i < timed.size(); ++i)
        {
            Line line;
            line.section = timed[i].section;
            line.lrcSeconds = timed[i].seconds;
            const auto clean = cleanLine(timed[i].body);
            const double written = buildWords(line, clean, englishRules);
            for (const auto &syllable : line.syllables)
                anyWordTimes = anyWordTimes || syllable.lrcSeconds >= 0.0;

            double end = i + 1 < timed.size() ? timed[i + 1].seconds : -1.0;
            const auto nextBreak = std::upper_bound(breaks.begin(), breaks.end(), line.lrcSeconds);
            if (nextBreak != breaks.end() && (end < 0.0 || *nextBreak < end))
                end = *nextBreak;
            if (written > line.lrcSeconds && (end < 0.0 || written < end))
                end = written;
            line.lrcEndSeconds = end;
            lyrics.lines.push_back(std::move(line));
        }
        lyrics.sourceTiming = anyWordTimes ? SourceTiming::Words : SourceTiming::Lines;
    }
    else
    {
        for (const auto &[body, section] : plain)
        {
            Line line;
            line.section = section;
            buildWords(line, cleanLine(body), englishRules);
            lyrics.lines.push_back(std::move(line));
        }
        lyrics.sourceTiming = SourceTiming::None;
    }
    return lyrics;
}

void KaraokeLyrics::applyTiming(const KaraokeReference *reference, double offsetSeconds)
{
    for (auto &line : lines)
    {
        for (auto &syllable : line.syllables)
        {
            syllable.startSeconds = syllable.endSeconds = -1.0;
            syllable.note = -1;
        }
        line.startSeconds = line.endSeconds = -1.0;
    }
    const Notes *notes =
        reference != nullptr && reference->isLoaded() ? &reference->notes : nullptr;

    if (sourceTiming == SourceTiming::None)
    {
        if (notes == nullptr)
            return;
        // Plain text: lay the lines onto the notes. Each line takes a run of
        // notes about as long as its syllable count; lines start after
        // pauses and should not contain long ones. Notes before the first
        // line and after the last may be left out (intro, ad-libs).
        std::vector<int> sung;
        for (int i = 0; i < static_cast<int>(lines.size()); ++i)
            if (!lines[static_cast<size_t>(i)].syllables.empty())
                sung.push_back(i);
        const int lineCount = static_cast<int>(sung.size());
        const int noteCount = static_cast<int>(notes->size());
        if (lineCount == 0 || noteCount < lineCount)
            return;

        constexpr double kSkipCost = 0.6;
        std::vector<double> excess(static_cast<size_t>(noteCount + 1), 0.0);
        for (int i = 0; i + 1 < noteCount; ++i)
            excess[static_cast<size_t>(i + 1)] =
                excess[static_cast<size_t>(i)] +
                std::max(0.0, gapBefore(*notes, static_cast<size_t>(i + 1)) - 0.5);
        auto internalPauses = [&](int a, int b) {
            return b - a >= 2 ? excess[static_cast<size_t>(b - 1)] - excess[static_cast<size_t>(a)] : 0.0;
        };

        const double inf = std::numeric_limits<double>::infinity();
        const int width = noteCount + 1;
        std::vector<double> cost(static_cast<size_t>((lineCount + 1) * width), inf);
        std::vector<int> from(cost.size(), -1);
        auto at = [width](int k, int j) { return static_cast<size_t>(k * width + j); };
        for (int j = 0; j <= noteCount; ++j)
            cost[at(0, j)] = kSkipCost * j;
        for (int k = 0; k < lineCount; ++k)
        {
            const int syllables =
                static_cast<int>(lines[static_cast<size_t>(sung[static_cast<size_t>(k)])].syllables.size());
            const int longest = 3 * syllables + 8;
            for (int a = k; a < noteCount; ++a)
            {
                const double base = cost[at(k, a)];
                if (base == inf)
                    continue;
                const double startReward = a == 0 ? 0.0 : 2.0 * std::min(gapBefore(*notes, static_cast<size_t>(a)), 1.5);
                for (int b = a + 1; b <= std::min(noteCount, a + longest); ++b)
                {
                    const int n = b - a;
                    const double mismatch = n >= syllables
                                                ? (n - syllables) / (syllables + 1.0)
                                                : 1.5 * (syllables - n) / (n + 1.0);
                    const double value = base + 3.0 * mismatch * mismatch +
                                         2.0 * internalPauses(a, b) - startReward;
                    if (value < cost[at(k + 1, b)])
                    {
                        cost[at(k + 1, b)] = value;
                        from[at(k + 1, b)] = a;
                    }
                }
            }
        }
        int end = -1;
        double bestTotal = inf;
        for (int j = lineCount; j <= noteCount; ++j)
        {
            const double total = cost[at(lineCount, j)] + kSkipCost * (noteCount - j);
            if (total < bestTotal)
            {
                bestTotal = total;
                end = j;
            }
        }
        if (end < 0)
            return;
        for (int k = lineCount; k > 0; --k)
        {
            const int start = from[at(k, end)];
            auto &line = lines[static_cast<size_t>(sung[static_cast<size_t>(k - 1)])];
            assignSyllables(line, 0, static_cast<int>(line.syllables.size()), *notes,
                            static_cast<size_t>(start), static_cast<size_t>(end),
                            (*notes)[static_cast<size_t>(start)].startSeconds,
                            (*notes)[static_cast<size_t>(end - 1)].endSeconds);
            end = start;
        }
        for (auto &line : lines)
            finishLine(line);
        return;
    }

    // LRC. Every written time moves by the user's offset minus the file's own.
    const double shift = offsetSeconds - fileOffsetSeconds;
    const size_t lineCount = lines.size();
    std::vector<size_t> firstNote(lineCount, 0);
    std::vector<size_t> endNote(lineCount, 0);
    if (notes != nullptr)
    {
        size_t from = 0;
        for (size_t k = 0; k < lineCount; ++k)
        {
            firstNote[k] = snapToNote(*notes, lines[k].lrcSeconds + shift, 0.6, 0.5, from);
            from = firstNote[k];
        }
        for (size_t k = 0; k < lineCount; ++k)
        {
            const auto &line = lines[k];
            size_t end = k + 1 < lineCount ? firstNote[k + 1] : notes->size();
            const bool endsBeforeNext =
                line.lrcEndSeconds >= 0.0 &&
                (k + 1 == lineCount || line.lrcEndSeconds < lines[k + 1].lrcSeconds - 1.0e-6);
            if (endsBeforeNext)
                end = std::min(end, snapToNote(*notes, line.lrcEndSeconds + shift, 0.4, 0.3, firstNote[k]));
            // A line is not sung for longer than its syllables can take:
            // notes far past it are an instrumental part or ad-libs.
            const double cutoff = line.lrcSeconds + shift + 1.2 * static_cast<double>(line.syllables.size()) + 4.0;
            end = std::min(end, firstNoteStartingAtOrAfter(*notes, cutoff, firstNote[k]));
            // Likewise a long pause once most of the line has its notes.
            const auto syllables = line.syllables.size();
            for (size_t i = firstNote[k] + 1; i < end; ++i)
                if (gapBefore(*notes, i) >= 1.2 && 4 * (i - firstNote[k]) >= 3 * syllables)
                {
                    end = i;
                    break;
                }
            endNote[k] = std::max(end, firstNote[k]);
        }
    }

    for (size_t k = 0; k < lineCount; ++k)
    {
        auto &line = lines[k];
        const int syllableCount = static_cast<int>(line.syllables.size());
        const double lineStart = line.lrcSeconds + shift;
        double lineEnd = line.lrcEndSeconds >= 0.0 ? line.lrcEndSeconds + shift
                                                   : lineStart + 0.45 * syllableCount + 1.0;
        lineEnd = std::max(lineEnd, lineStart + 0.1);
        if (syllableCount == 0)
        {
            line.startSeconds = lineStart;
            line.endSeconds = std::min(lineEnd, lineStart + 2.0);
            continue;
        }

        // Segments start at the line and at every syllable with its own time.
        struct Anchor
        {
            int syllable;
            double seconds;
            size_t note;
        };
        std::vector<Anchor> anchors;
        const auto &firstSyllable = line.syllables.front();
        anchors.push_back({0, firstSyllable.lrcSeconds >= 0.0 ? firstSyllable.lrcSeconds + shift : lineStart,
                           firstNote[k]});
        for (int s = 1; s < syllableCount; ++s)
        {
            const double written = line.syllables[static_cast<size_t>(s)].lrcSeconds;
            if (written >= 0.0)
                anchors.push_back({s, written + shift, 0});
        }
        if (notes != nullptr)
            for (size_t a = 1; a < anchors.size(); ++a)
                anchors[a].note = juce::jlimit(
                    anchors[a - 1].note, endNote[k],
                    snapToNote(*notes, anchors[a].seconds, 0.25, 0.25, anchors[a - 1].note));

        for (size_t a = 0; a < anchors.size(); ++a)
        {
            const bool lastSegment = a + 1 == anchors.size();
            const int s0 = anchors[a].syllable;
            const int s1 = lastSegment ? syllableCount : anchors[a + 1].syllable;
            const double t0 = anchors[a].seconds;
            double t1 = lastSegment ? lineEnd : anchors[a + 1].seconds;
            if (lastSegment)
                t1 = std::min(t1 - 0.1, t0 + 0.4 * (s1 - s0) + 0.4);
            if (notes != nullptr)
                assignSyllables(line, s0, s1, *notes, anchors[a].note,
                                lastSegment ? endNote[k] : anchors[a + 1].note, t0, t1);
            else
                spreadSyllables(line, s0, s1, t0, t1);
        }
        finishLine(line);
    }
}

std::optional<double> KaraokeLyrics::estimateOffset(const KaraokeReference &reference,
                                                    bool onlyIfClearlyBetter,
                                                    double maxShiftSeconds) const
{
    if (sourceTiming == SourceTiming::None || !reference.isLoaded() || lines.empty())
        return std::nullopt;

    // Phrase starts: notes after a breath.
    const auto &notes = reference.notes;
    std::vector<double> onsets;
    for (size_t i = 0; i < notes.size(); ++i)
        if (gapBefore(notes, i) >= 0.25)
            onsets.push_back(notes[i].startSeconds);
    if (onsets.empty())
        return std::nullopt;

    std::vector<double> written;
    for (const auto &line : lines)
        if (!line.syllables.empty())
            written.push_back(line.lrcSeconds - fileOffsetSeconds);
    if (written.empty())
        return std::nullopt;

    auto score = [&](double offset) {
        double total = 0.0;
        for (const double time : written)
        {
            const double t = time + offset + 0.05;
            const auto it = std::lower_bound(onsets.begin(), onsets.end(), t);
            double nearest = std::numeric_limits<double>::infinity();
            if (it != onsets.end())
                nearest = *it - t;
            if (it != onsets.begin() && std::abs(*(it - 1) - t) < std::abs(nearest))
                nearest = *(it - 1) - t;
            total += std::exp(-(nearest * nearest) / (2.0 * 0.18 * 0.18));
        }
        return total;
    };
    auto withPrior = [&](double offset) { return score(offset) - 0.01 * std::abs(offset); };

    double best = 0.0;
    double bestValue = withPrior(0.0);
    for (double offset = -maxShiftSeconds; offset <= maxShiftSeconds; offset += 0.02)
    {
        const double value = withPrior(offset);
        if (value > bestValue)
        {
            bestValue = value;
            best = offset;
        }
    }
    const double coarse = best;
    for (double offset = coarse - 0.03; offset <= coarse + 0.03; offset += 0.002)
    {
        const double value = withPrior(offset);
        if (value > bestValue)
        {
            bestValue = value;
            best = offset;
        }
    }

    if (onlyIfClearlyBetter)
    {
        const double margin = std::max(1.0, 0.1 * static_cast<double>(written.size()));
        if (score(best) < score(0.0) + margin)
            return std::nullopt;
    }
    return std::round(best * 1000.0) / 1000.0;
}

int KaraokeLyrics::nextTimedLine(int line) const
{
    for (int i = std::max(-1, line) + 1; i < static_cast<int>(lines.size()); ++i)
        if (lines[static_cast<size_t>(i)].isTimed())
            return i;
    return -1;
}

int KaraokeLyrics::previousTimedLine(int line) const
{
    for (int i = std::min(line, static_cast<int>(lines.size())) - 1; i >= 0; --i)
        if (lines[static_cast<size_t>(i)].isTimed())
            return i;
    return -1;
}

int KaraokeLyrics::currentLineAt(double referenceSeconds) const
{
    constexpr double kHoldSeconds = 0.5;
    int current = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i)
    {
        const auto &line = lines[static_cast<size_t>(i)];
        if (line.isTimed() && line.startSeconds <= referenceSeconds)
            current = i;
    }
    if (current < 0)
        return nextTimedLine(-1);
    if (referenceSeconds > lines[static_cast<size_t>(current)].endSeconds + kHoldSeconds)
    {
        const int next = nextTimedLine(current);
        if (next >= 0)
            return next;
    }
    return current;
}
