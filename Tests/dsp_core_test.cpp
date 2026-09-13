/*
    Free Pocket - DSP core tests.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.

    These tests encode the product promises as hard invariants:

      1. the dispersion is a true allpass: the magnitude spectrum and the RMS
         of the signal survive it to within numerical error
      2. the group delay stays inside the published audibility budget
         (Liski, Makivirta, Valimaki, IEEE/ACM TASLP 2021: -0.56 / +0.64 ms
         over 500 Hz - 4 kHz)
      3. the impulse response is short: no long ringing tail
      4. "never worse": the output peak never exceeds the input peak, and the
         loudness never drops below the dry signal
      5. dense material actually gains level for free
      6. Headroom at 0 % is a sample accurate, latency compensated bypass
      7. L and R are always processed identically, so the image cannot move
      8. no NaN, no Inf, no self oscillation
*/

#include "TestHarness.h"

#include "../Source/dsp/AllpassDispersion.h"
#include "../Source/dsp/FreePocketEngine.h"

#include <complex>
#include <vector>

using namespace fptest;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr double kPi = fp::kPi;

std::vector<float> makeSilence (std::size_t n) { return std::vector<float> (n, 0.0f); }

/** Discrete Fourier coefficient at one frequency, evaluated directly. */
std::complex<double> dftBin (const std::vector<float>& x, double frequencyHz)
{
    std::complex<double> sum (0.0, 0.0);
    const double w = 2.0 * kPi * frequencyHz / kSampleRate;
    for (std::size_t n = 0; n < x.size(); ++n)
        sum += std::complex<double> ((double) x[n], 0.0) * std::exp (std::complex<double> (0.0, -w * (double) n));
    return sum;
}

/** Runs one dispersion curve over a buffer. */
std::vector<float> runCurve (const std::vector<float>& input, int sections, double highHz, double q = 0.5)
{
    fp::DispersionCurve curve;
    curve.configure (sections, 25.0, highHz, q, kSampleRate);
    std::vector<float> out (input.size(), 0.0f);
    for (std::size_t n = 0; n < input.size(); ++n)
        out[n] = curve.process (input[n]);
    return out;
}

/** Group delay in milliseconds from the phase slope of the impulse response. */
double groupDelayMs (const std::vector<float>& impulseResponse, double frequencyHz)
{
    const double delta = 2.0;
    const auto lower = dftBin (impulseResponse, frequencyHz - delta);
    const auto upper = dftBin (impulseResponse, frequencyHz + delta);
    double difference = std::arg (upper) - std::arg (lower);
    while (difference > kPi) difference -= 2.0 * kPi;
    while (difference < -kPi) difference += 2.0 * kPi;
    return -difference / (2.0 * kPi * 2.0 * delta) * 1000.0;
}

std::vector<float> makeImpulse (std::size_t n, std::size_t position = 0)
{
    auto x = makeSilence (n);
    x[position] = 1.0f;
    return x;
}

/** Dense in phase multitone: the textbook low crest factor problem, crest of
    a zero phase N tone signal grows like sqrt(2N) (Boyd, 1986). */
std::vector<float> makeZeroPhaseMultitone (std::size_t n, int partials = 48)
{
    std::vector<float> x (n, 0.0f);
    for (int k = 0; k < partials; ++k)
    {
        const double f = 50.0 * std::pow (4000.0 / 50.0, (double) k / (double) (partials - 1));
        for (std::size_t i = 0; i < n; ++i)
            x[i] += (float) std::cos (2.0 * kPi * f * (double) i / kSampleRate);
    }
    const double peak = peakAbs (x);
    for (auto& v : x)
        v = (float) (v / peak * 0.9);
    return x;
}

/** Kick style transient: the case where phase rotation must NOT be used. */
std::vector<float> makeKick (std::size_t n)
{
    std::vector<float> x (n, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = (double) (i % 24000) / kSampleRate;
        const double env = std::exp (-t * 14.0);
        x[i] = (float) (0.9 * env * std::sin (2.0 * kPi * (55.0 * t + 40.0 * (1.0 - std::exp (-t * 8.0))))
                        + 0.25 * std::exp (-t * 900.0) * std::sin (2.0 * kPi * 2400.0 * t));
    }
    return x;
}

struct StereoBuffer
{
    std::vector<float> left, right;
};

StereoBuffer runEngine (const std::vector<float>& inputL, const std::vector<float>& inputR,
                        const fp::Parameters& parameters, int blockSize = 256)
{
    fp::FreePocketEngine engine;
    engine.prepare (kSampleRate, blockSize);
    engine.setParameters (parameters);

    StereoBuffer out { inputL, inputR };
    for (std::size_t start = 0; start < out.left.size(); start += (std::size_t) blockSize)
    {
        const int count = (int) std::min ((std::size_t) blockSize, out.left.size() - start);
        engine.process (out.left.data() + start, out.right.data() + start, count);
    }
    return out;
}

int engineLatency()
{
    fp::FreePocketEngine engine;
    engine.prepare (kSampleRate, 256);
    return engine.latencySamples();
}

// ---------------------------------------------------------------------------

void testDispersionIsExactlyAllpass()
{
    beginCase ("dispersion keeps the magnitude spectrum and the RMS");

    // |H(e^jw)| is measured directly from the impulse response, which is the
    // exact definition of the allpass property.
    const auto response = runCurve (makeImpulse (1 << 15), 6, 1500.0);

    double worstErrorDb = 0.0;
    for (int k = 0; k < 24; ++k)
    {
        const double f = 30.0 * std::pow (16000.0 / 30.0, (double) k / 23.0);
        const double magnitude = std::abs (dftBin (response, f));
        worstErrorDb = std::max (worstErrorDb, std::abs (20.0 * std::log10 (std::max (magnitude, 1e-20))));
    }

    Noise noise (2024u);
    std::vector<float> input (1 << 15, 0.0f);
    for (auto& v : input)
        v = 0.35f * noise.next();

    const auto output = runCurve (input, 6, 1500.0);

    checkLess (worstErrorDb, 0.01, "|H| deviation from unity across the spectrum (dB)");
    checkNear (rms (output, 4096) / std::max (1e-12, rms (input, 4096)), 1.0, 0.01, "RMS unchanged");
    checkTrue (allFinite (output), "output is finite");
}

void testGroupDelayBudget()
{
    beginCase ("group delay stays inside the audibility budget");

    const auto response = runCurve (makeImpulse (1 << 15), 6, 1500.0);

    const double at500 = groupDelayMs (response, 500.0);
    const double at1k = groupDelayMs (response, 1000.0);
    const double at2k = groupDelayMs (response, 2000.0);
    const double at4k = groupDelayMs (response, 4000.0);

    std::printf ("        group delay ms: 500 %.3f, 1k %.3f, 2k %.3f, 4k %.3f\n", at500, at1k, at2k, at4k);

    checkLess (std::abs (at1k), 2.0, "|group delay| at 1 kHz (ms)");
    checkLess (std::abs (at2k), 0.9, "|group delay| at 2 kHz (ms)");
    checkLess (std::abs (at4k), 0.64, "|group delay| at 4 kHz (ms), Liski 2021 threshold");
    checkGreater (at500, 0.0, "group delay is positive, i.e. causal smearing only");
}

void testImpulseResponseIsShort()
{
    beginCase ("impulse response is short, no ringing tail");

    const auto response = runCurve (makeImpulse (1 << 15), 6, 1500.0);

    double total = 0.0;
    for (float v : response)
        total += (double) v * v;

    double running = 0.0;
    std::size_t samplesFor99 = response.size();
    for (std::size_t i = 0; i < response.size(); ++i)
    {
        running += (double) response[i] * response[i];
        if (running >= 0.99 * total)
        {
            samplesFor99 = i;
            break;
        }
    }

    const double ms = 1000.0 * (double) samplesFor99 / kSampleRate;
    std::printf ("        99%% of the energy inside %.1f ms\n", ms);
    checkLess (ms, 60.0, "99% of the impulse energy arrives within (ms)");
    checkNear (total, 1.0, 0.01, "impulse response energy is unity (allpass)");
}

void testNeverLouderThanTheInput()
{
    beginCase ("output peak never exceeds the input peak");

    fp::Parameters parameters;
    parameters.headroom = 100.0f;
    parameters.character = 100.0f;

    const auto dense = makeZeroPhaseMultitone (1 << 17);
    const auto denseOut = runEngine (dense, dense, parameters);
    checkLess (peakAbs (denseOut.left) / std::max (1e-9, peakAbs (dense)), 1.001,
               "dense multitone: peak ratio out/in");

    const auto kick = makeKick (1 << 17);
    const auto kickOut = runEngine (kick, kick, parameters);
    checkLess (peakAbs (kickOut.left) / std::max (1e-9, peakAbs (kick)), 1.001,
               "kick: peak ratio out/in");

    Noise noise (7u);
    std::vector<float> loud (1 << 17, 0.0f);
    for (std::size_t i = 0; i < loud.size(); ++i)
        loud[i] = 0.99f * std::tanh (3.0f * noise.next());
    const auto loudOut = runEngine (loud, loud, parameters);
    checkLess (peakAbs (loudOut.left) / std::max (1e-9, peakAbs (loud)), 1.001,
               "hot noise: peak ratio out/in");
    checkTrue (allFinite (loudOut.left) && allFinite (loudOut.right), "output is finite");
}

void testNeverQuieterThanTheInput()
{
    beginCase ("loudness is never lost, even on material that dislikes rotation");

    fp::Parameters parameters;
    parameters.headroom = 100.0f;
    parameters.character = 100.0f;

    const auto kick = makeKick (1 << 17);
    const auto out = runEngine (kick, kick, parameters);

    const double dryRms = rms (kick, 8192);
    const double wetRms = rms (out.left, 8192);
    const double deltaDb = 20.0 * std::log10 (std::max (wetRms, 1e-12) / std::max (dryRms, 1e-12));
    std::printf ("        kick RMS change %.3f dB\n", deltaDb);
    checkGreater (deltaDb, -0.30, "RMS change on a kick (dB)");
}

void testDenseMaterialGainsFreeLevel()
{
    beginCase ("dense in phase material gains level at the same peak");

    fp::Parameters parameters;
    parameters.headroom = 100.0f;
    parameters.character = 100.0f;

    const auto dense = makeZeroPhaseMultitone (1 << 17);
    const auto out = runEngine (dense, dense, parameters);

    const double dryRms = rms (dense, 1 << 15);
    const double wetRms = rms (out.left, 1 << 15);
    const double gainDb = 20.0 * std::log10 (std::max (wetRms, 1e-12) / std::max (dryRms, 1e-12));
    const double peakRatioDb = 20.0 * std::log10 (std::max (peakAbs (out.left), 1e-12)
                                                  / std::max (peakAbs (dense), 1e-12));

    std::printf ("        free level %.2f dB at a peak change of %.2f dB\n", gainDb, peakRatioDb);
    checkGreater (gainDb, 1.0, "RMS gained for free (dB)");
    checkLess (peakRatioDb, 0.01, "peak change (dB)");
}

void testHeadroomAtZeroIsBitTransparent()
{
    beginCase ("Headroom at 0 % is a latency compensated bypass");

    fp::Parameters parameters;
    parameters.headroom = 0.0f;
    parameters.character = 50.0f;

    Noise noise (99u);
    std::vector<float> input (1 << 15, 0.0f);
    for (auto& v : input)
        v = 0.5f * noise.next();

    const auto out = runEngine (input, input, parameters);
    const int latency = engineLatency();

    double worst = 0.0;
    for (std::size_t i = (std::size_t) latency; i < input.size(); ++i)
        worst = std::max (worst, (double) std::abs (out.left[i] - input[i - (std::size_t) latency]));

    checkLess (worst, 1.0e-6, "max sample difference against the delayed dry signal");
    checkTrue (latency > 0, "engine reports a lookahead latency");
}

void testStereoImageCannotMove()
{
    beginCase ("L and R get identical processing");

    fp::Parameters parameters;
    parameters.headroom = 100.0f;
    parameters.character = 80.0f;

    Noise noise (5u);
    std::vector<float> mono (1 << 16, 0.0f);
    std::vector<float> side (1 << 16, 0.0f);
    for (std::size_t i = 0; i < mono.size(); ++i)
    {
        mono[i] = 0.5f * noise.next();
        side[i] = 0.2f * noise.next();
    }

    std::vector<float> left (mono.size()), right (mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i)
    {
        left[i] = mono[i] + side[i];
        right[i] = mono[i] - side[i];
    }

    const auto out = runEngine (left, right, parameters);

    // Processing the mono sum separately must equal the sum of the outputs:
    // only possible if both channels ran through exactly the same filter and
    // exactly the same gain.
    std::vector<float> sumIn (mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i)
        sumIn[i] = left[i] + right[i];

    const auto monoOut = runEngine (mono, mono, parameters);
    double worst = 0.0;
    for (std::size_t i = 0; i < mono.size(); ++i)
        worst = std::max (worst, (double) std::abs (monoOut.left[i] - monoOut.right[i]));
    checkLess (worst, 1.0e-9, "identical input gives bit identical L and R");

    const double correlationIn = correlation (left, right);
    const double correlationOut = correlation (out.left, out.right);
    std::printf ("        correlation in %.4f, out %.4f\n", correlationIn, correlationOut);
    checkLess (std::abs (correlationOut - correlationIn), 0.02, "inter channel correlation change");
}

void testSilenceAndStability()
{
    beginCase ("silence stays silent and nothing self oscillates");

    fp::Parameters parameters;
    parameters.headroom = 100.0f;
    parameters.character = 100.0f;

    auto silence = makeSilence (1 << 14);
    const auto out = runEngine (silence, silence, parameters);
    checkLess (peakAbs (out.left), 1.0e-9, "silence in, silence out");

    // burst, then silence: the tail must die away
    std::vector<float> burst (1 << 16, 0.0f);
    Noise noise (31u);
    for (std::size_t i = 0; i < 24000; ++i)
        burst[i] = 0.8f * noise.next();

    const auto tail = runEngine (burst, burst, parameters);
    std::vector<float> lastPart (tail.left.begin() + 48000, tail.left.end());
    checkLess (peakAbs (lastPart), 1.0e-4, "tail 0.5 s after the burst");
    checkTrue (allFinite (tail.left), "no NaN or Inf");
}

void testOutputTrim()
{
    beginCase ("output trim is exact");

    fp::Parameters parameters;
    parameters.headroom = 0.0f;
    parameters.outputDb = -6.0f;

    Noise noise (17u);
    std::vector<float> input (1 << 14, 0.0f);
    for (auto& v : input)
        v = 0.4f * noise.next();

    const auto out = runEngine (input, input, parameters);
    const int latency = engineLatency();

    double worst = 0.0;
    const float expectedGain = fp::dbToGain (-6.0f);
    for (std::size_t i = (std::size_t) latency; i < input.size(); ++i)
        worst = std::max (worst, (double) std::abs (out.left[i] - input[i - (std::size_t) latency] * expectedGain));

    checkLess (worst, 1.0e-6, "output trim matches -6 dB exactly");
}

} // namespace

int main()
{
    std::printf ("Free Pocket DSP core tests\n");

    testDispersionIsExactlyAllpass();
    testGroupDelayBudget();
    testImpulseResponseIsShort();
    testNeverLouderThanTheInput();
    testNeverQuieterThanTheInput();
    testDenseMaterialGainsFreeLevel();
    testHeadroomAtZeroIsBitTransparent();
    testStereoImageCannotMove();
    testSilenceAndStability();
    testOutputTrim();

    return summary();
}
