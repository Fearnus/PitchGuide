// GAME note detection gives the same notes for the same audio: the segmenter's
// random op is seeded in memory, and every analysis starts from that seed.

#include "../Source/Audio/GAMEDetector.h"
#include "TestAssert.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using Bytes = std::vector<uint8_t>;

void varint(Bytes &out, uint64_t value)
{
    while (value >= 0x80)
    {
        out.push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

void bytesField(Bytes &out, uint32_t number, const Bytes &payload)
{
    varint(out, (static_cast<uint64_t>(number) << 3) | 2);
    varint(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

void stringField(Bytes &out, uint32_t number, const std::string &text)
{
    bytesField(out, number, Bytes(text.begin(), text.end()));
}

void varintField(Bytes &out, uint32_t number, uint64_t value)
{
    varint(out, static_cast<uint64_t>(number) << 3);
    varint(out, value);
}

void floatField(Bytes &out, uint32_t number, float value)
{
    varint(out, (static_cast<uint64_t>(number) << 3) | 5);
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int byte = 0; byte < 4; ++byte)
        out.push_back(static_cast<uint8_t>(bits >> (8 * byte)));
}

Bytes node(const std::string &opType, const std::vector<Bytes> &attributes = {},
           const std::string &domain = {})
{
    Bytes out;
    stringField(out, 1, "in");      // input
    stringField(out, 2, opType + "_out"); // output
    stringField(out, 3, opType);    // name
    stringField(out, 4, opType);    // op_type
    for (const auto &attribute : attributes)
        bytesField(out, 5, attribute);
    if (!domain.empty())
        stringField(out, 7, domain);
    return out;
}

Bytes seedAttribute(float seed)
{
    Bytes out;
    stringField(out, 1, "seed");
    floatField(out, 2, seed);
    varintField(out, 20, 1); // FLOAT
    return out;
}

size_t countSeedAttributes(const Bytes &model, float seed)
{
    Bytes pattern;
    bytesField(pattern, 5, seedAttribute(seed));
    size_t count = 0;
    for (size_t i = 0; i + pattern.size() <= model.size(); ++i)
        if (std::memcmp(model.data() + i, pattern.data(), pattern.size()) == 0)
            ++count;
    return count;
}

void testSeedingRewritesTheModel()
{
    // A loop body (subgraph attribute) with a RandomNormal in it.
    Bytes body;
    bytesField(body, 1, node("RandomNormal"));
    Bytes bodyAttribute;
    stringField(bodyAttribute, 1, "body");
    bytesField(bodyAttribute, 6, body); // g
    varintField(bodyAttribute, 20, 5);  // GRAPH

    Bytes graph;
    bytesField(graph, 1, node("RandomUniformLike"));
    bytesField(graph, 1, node("Loop", {bodyAttribute}));
    bytesField(graph, 1, node("RandomUniform", {seedAttribute(7.0f)})); // already seeded
    bytesField(graph, 1, node("RandomUniformLike", {}, "com.example")); // not ONNX's op
    bytesField(graph, 1, node("Add"));
    stringField(graph, 2, "graph");

    Bytes model;
    varintField(model, 1, 8);              // ir_version
    stringField(model, 2, "test");         // producer_name
    bytesField(model, 7, graph);           // graph
    Bytes opset;
    varintField(opset, 2, 17);
    bytesField(model, 8, opset);           // opset_import

    Bytes seeded = model;
    CHECK(GAMEDetector::seedRandomOps(seeded, 1234.0f) == 2);
    CHECK(seeded.size() > model.size());
    CHECK(countSeedAttributes(seeded, 1234.0f) == 2);
    CHECK(countSeedAttributes(seeded, 7.0f) == 1);

    // The rewritten bytes parse again and have nothing left to seed.
    Bytes again = seeded;
    CHECK(GAMEDetector::seedRandomOps(again, 1234.0f) == 0);
    CHECK(again == seeded);

    // Not a model: reported, and left alone.
    Bytes garbage = {0xFF, 0xFF, 0xFF};
    const Bytes original = garbage;
    CHECK(GAMEDetector::seedRandomOps(garbage, 1.0f) == -1);
    CHECK(garbage == original);
    Bytes empty;
    CHECK(GAMEDetector::seedRandomOps(empty, 1.0f) == -1);
}

juce::File gameModelDirectory()
{
#ifdef PITCHNET_PROJECT_RESOURCES_DIR
    return juce::File(juce::String(PITCHNET_PROJECT_RESOURCES_DIR))
        .getChildFile("models")
        .getChildFile("GAME");
#else
    return {};
#endif
}

/** Sung-like phrases: harmonic tones on a melody, with breaths between. */
std::vector<float> phrases(int sampleRate)
{
    const float melody[] = {57, 59, 60, 62, 64, 62, 60, 59, 57, 55, 57, 60, 64, 65, 64, 62};
    std::vector<float> audio;
    double phase = 0.0;
    for (int phrase = 0; phrase < 4; ++phrase)
    {
        for (int n = 0; n < 4; ++n)
        {
            const float midi = melody[phrase * 4 + n];
            const double hz = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
            const int length = sampleRate * (n == 3 ? 7 : 4) / 10;
            for (int i = 0; i < length; ++i)
            {
                const double t = static_cast<double>(i) / sampleRate;
                const double vibrato = 1.0 + 0.004 * std::sin(2.0 * 3.14159265 * 5.5 * t);
                phase += 2.0 * 3.14159265 * hz * vibrato / sampleRate;
                double value = 0.0;
                for (int k = 1; k <= 6; ++k)
                    value += std::sin(phase * k) / (k * k);
                const double fade = std::min(1.0, std::min(i, length - i) / (0.02 * sampleRate));
                audio.push_back(static_cast<float>(0.3 * value * fade));
            }
        }
        audio.insert(audio.end(), static_cast<size_t>(sampleRate * 6 / 10), 0.0f);
    }
    return audio;
}

bool sameNotes(const std::vector<GAMEDetector::NoteEvent> &a,
               const std::vector<GAMEDetector::NoteEvent> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].startFrame != b[i].startFrame || a[i].endFrame != b[i].endFrame ||
            a[i].midiNote != b[i].midiNote || a[i].isRest != b[i].isRest)
            return false;
    return true;
}

void testRealModelIsSeededAndRepeatable()
{
    const auto directory = gameModelDirectory();
    const auto segmenterFile = directory.getChildFile("segmenter.onnx");
    if (!segmenterFile.existsAsFile())
    {
        std::printf("GAME models not found at %s: skipping the model checks\n",
                    directory.getFullPathName().toRawUTF8());
        return;
    }

    juce::MemoryBlock block;
    CHECK(segmenterFile.loadFileAsData(block));
    const auto *first = static_cast<const uint8_t *>(block.getData());
    Bytes segmenter(first, first + block.getSize());
    CHECK(GAMEDetector::seedRandomOps(segmenter, 1234.0f) == 1);

#ifdef HAVE_ONNXRUNTIME
    const int sampleRate = GAMEDetector::SAMPLE_RATE;
    const auto audio = phrases(sampleRate);

    GAMEDetector detector;
    CHECK(detector.loadModels(directory, GPUProvider::CPU, 0));
    const auto firstRun = detector.detectNotes(audio.data(), static_cast<int>(audio.size()),
                                               sampleRate);
    CHECK(!firstRun.empty());
    for (int run = 0; run < 2; ++run)
        CHECK(sameNotes(firstRun, detector.detectNotes(audio.data(),
                                                       static_cast<int>(audio.size()),
                                                       sampleRate)));

    // A separate detector (a new process, a reopened plug-in) agrees too.
    GAMEDetector other;
    CHECK(other.loadModels(directory, GPUProvider::CPU, 0));
    CHECK(sameNotes(firstRun, other.detectNotes(audio.data(), static_cast<int>(audio.size()),
                                                sampleRate)));
    std::printf("GAME: %d events, identical over 4 analyses\n",
                static_cast<int>(firstRun.size()));
#endif
}
} // namespace

int main()
{
    testSeedingRewritesTheModel();
    testRealModelIsSeededAndRepeatable();
    std::printf("GameDeterminismTests passed\n");
    return 0;
}
