#include "LivePitchDetector.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kTargetAnalysisRate = 16000.0;
constexpr double kPi = 3.14159265358979323846;

int nextPowerOfTwo(int value)
{
    int power = 1;
    while (power < value)
        power <<= 1;
    return power;
}

float median3(float a, float b, float c)
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
} // namespace

void LivePitchDetector::prepare(double hostSampleRate, const Settings &newSettings)
{
    settings = newSettings;
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    decimation = std::max(1, static_cast<int>(std::lround(hostRate / kTargetAnalysisRate)));
    analysisRate = hostRate / decimation;

    // Fourth-order Butterworth low-pass (two RBJ sections) at the host rate:
    // keeps aliasing out of the decimated signal, and the harmonics above a
    // few kHz carry nothing YIN needs.
    const double cutoff = std::min(4000.0, 0.25 * analysisRate);
    const double w0 = 2.0 * kPi * cutoff / hostRate;
    const double cosW0 = std::cos(w0);
    const double sinW0 = std::sin(w0);
    const double qs[2] = {0.54119610, 1.30656296};
    for (int section = 0; section < 2; ++section)
    {
        const double alpha = sinW0 / (2.0 * qs[section]);
        const double a0 = 1.0 + alpha;
        auto &filter = antiAlias[static_cast<size_t>(section)];
        filter.b0 = static_cast<float>((1.0 - cosW0) * 0.5 / a0);
        filter.b1 = static_cast<float>((1.0 - cosW0) / a0);
        filter.b2 = filter.b0;
        filter.a1 = static_cast<float>(-2.0 * cosW0 / a0);
        filter.a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    tauMin = std::max(2, static_cast<int>(std::floor(analysisRate / settings.maxHz)));
    tauMax = std::max(tauMin + 2,
                      static_cast<int>(std::ceil(analysisRate / settings.minHz)) + 1);
    integration = tauMax;
    windowSize = integration + tauMax;
    hopSize = std::max(1, static_cast<int>(std::lround(settings.hopSeconds * analysisRate)));

    ring.assign(static_cast<size_t>(nextPowerOfTwo(windowSize * 2)), 0.0f);
    ringMask = static_cast<int>(ring.size()) - 1;
    window.assign(static_cast<size_t>(windowSize), 0.0f);
    difference.assign(static_cast<size_t>(tauMax + 2), 0.0f);
    cmndf.assign(static_cast<size_t>(tauMax + 2), 1.0f);

    prepared = true;
    reset();
}

void LivePitchDetector::reset()
{
    for (auto &filter : antiAlias)
        filter.reset();
    std::fill(ring.begin(), ring.end(), 0.0f);
    decimationPhase = 0;
    ringWrite = 0;
    filled = 0;
    sinceLastFrame = 0;
    streamSamples = 0;
    historyCount = 0;
}

bool LivePitchDetector::pushSample(float sample)
{
    const float filtered = antiAlias[1].process(antiAlias[0].process(sample));
    if (++decimationPhase < decimation)
        return false;
    decimationPhase = 0;

    ring[static_cast<size_t>(ringWrite)] = filtered;
    ringWrite = (ringWrite + 1) & ringMask;
    if (filled < windowSize)
        ++filled;
    if (++sinceLastFrame < hopSize)
        return false;
    sinceLastFrame = 0;
    return filled >= windowSize;
}

bool LivePitchDetector::analyse(LivePitchFrame &frame)
{
    const int start = (ringWrite - windowSize) & ringMask;
    const int firstPart = std::min(windowSize, static_cast<int>(ring.size()) - start);
    std::copy(ring.begin() + start, ring.begin() + start + firstPart, window.begin());
    std::copy(ring.begin(), ring.begin() + (windowSize - firstPart),
              window.begin() + firstPart);

    LivePitchFrame raw;
    raw.midi = detectWindow(window.data(), raw.confidence, raw.levelDb);

    if (!settings.medianSmoothing)
    {
        frame = raw;
        return true;
    }

    if (historyCount < 2)
    {
        history[static_cast<size_t>(historyCount++)] = raw;
        return false;
    }

    // Report the middle of the last three frames.
    const auto &older = history[0];
    const auto &middle = history[1];
    frame = middle;
    if (older.isVoiced() && middle.isVoiced() && raw.isVoiced())
    {
        frame.midi = median3(older.midi, middle.midi, raw.midi);
    }
    else if (middle.isVoiced() && !older.isVoiced() && !raw.isVoiced())
    {
        // 5 ms of "voice" between silences is a detector blip, not singing.
        frame.midi = 0.0f;
    }
    else if (!middle.isVoiced() && older.isVoiced() && raw.isVoiced() &&
             std::abs(older.midi - raw.midi) < 1.0f && middle.levelDb >= settings.gateDb)
    {
        // Bridge a one-frame dropout inside a held note.
        frame.midi = 0.5f * (older.midi + raw.midi);
        frame.confidence = std::min(older.confidence, raw.confidence);
    }

    history[0] = history[1];
    history[1] = raw;
    return true;
}

float LivePitchDetector::detectWindow(const float *x, float &confidence, float &levelDb)
{
    confidence = 0.0f;

    double energy = 0.0;
    for (int j = 0; j < windowSize; ++j)
        energy += static_cast<double>(x[j]) * x[j];
    const double rms = std::sqrt(energy / windowSize);
    levelDb = rms > 1.0e-9 ? static_cast<float>(20.0 * std::log10(rms)) : -180.0f;
    if (levelDb < settings.gateDb)
        return 0.0f;

    // YIN difference function d(tau) = sum (x[j] - x[j + tau])^2 over one
    // integration length, then its cumulative mean normalised form.
    difference[0] = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const float *a = x;
        const float *b = x + tau;
        float sum = 0.0f;
        for (int j = 0; j < integration; ++j)
        {
            const float delta = a[j] - b[j];
            sum += delta * delta;
        }
        difference[static_cast<size_t>(tau)] = sum;
    }

    cmndf[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += difference[static_cast<size_t>(tau)];
        cmndf[static_cast<size_t>(tau)] =
            running > 1.0e-12
                ? static_cast<float>(difference[static_cast<size_t>(tau)] * tau / running)
                : 1.0f;
    }

    // The first dip below the threshold, followed down to its minimum. Taking
    // the first rather than the deepest is what keeps YIN off the sub-octave.
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (cmndf[static_cast<size_t>(tau)] < settings.yinThreshold)
        {
            while (tau + 1 <= tauMax &&
                   cmndf[static_cast<size_t>(tau + 1)] < cmndf[static_cast<size_t>(tau)])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin + 1; tau <= tauMax; ++tau)
            if (cmndf[static_cast<size_t>(tau)] < cmndf[static_cast<size_t>(best)])
                best = tau;
    }

    const float aperiodicity = cmndf[static_cast<size_t>(best)];
    confidence = std::clamp(1.0f - aperiodicity, 0.0f, 1.0f);
    if (aperiodicity > settings.maxAperiodicity || best >= tauMax)
        return 0.0f;

    double refined = best;
    if (best > tauMin)
    {
        const double s0 = cmndf[static_cast<size_t>(best - 1)];
        const double s1 = cmndf[static_cast<size_t>(best)];
        const double s2 = cmndf[static_cast<size_t>(best + 1)];
        const double denominator = s0 - 2.0 * s1 + s2;
        if (std::abs(denominator) > 1.0e-12)
            refined += std::clamp(0.5 * (s0 - s2) / denominator, -0.5, 0.5);
    }

    const double hz = analysisRate / refined;
    if (!std::isfinite(hz) || hz < settings.minHz * 0.97 || hz > settings.maxHz * 1.03)
        return 0.0f;
    return static_cast<float>(69.0 + 12.0 * std::log2(hz / 440.0));
}
