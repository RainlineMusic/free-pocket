/*
    Free Pocket - dispersion engine.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.

    Signal flow, per channel, identical filters on L and R so the stereo image
    can never move:

        x --> bank of allpass curves (magnitude exact, phase rotated)
          --> curve selector (windowed peak, identity included, hysteresis)
          --> lookahead peak guard --> makeup gain --> output trim

    Why it works
    ------------
    An allpass cascade does not change the magnitude spectrum or the RMS of a
    signal, only the relative timing of its partials. For material whose
    partials happen to line up in phase (bass with in phase harmonics, dense
    limited mixes, multitones) that alignment is what creates the peak, and
    rotating the phase pulls the peak down by 1 to 3 dB while the loudness
    stays exactly the same. The recovered peak room is then given back as
    gain: same spectrum, same peak, more level. That is the "free" part.

    Why it is adaptive
    ------------------
    Measurements (see docs/theory.md) show that a single fixed rotation makes
    the peak WORSE on a lot of real material: a kick loses about 2 dB, a bass
    pluck up to 3 dB. So the engine runs several curves in parallel, measures
    the true peak each of them would produce over a sliding window, and keeps
    whichever wins - including the identity curve. Switching is rate limited
    and crossfaded, so the choice moves at musical speed, not per sample.

    Why it cannot overshoot
    -----------------------
    The output stage is not a limiter: it never changes the waveform. It only
    refuses to apply more gain than the lookahead window allows, which makes
    "output peak <= input peak" an invariant of the structure.
*/

#pragma once

#include "AllpassDispersion.h"

#include <array>
#include <vector>

namespace fp
{

inline constexpr int kLookaheadSamplesAt48k = 256;
inline constexpr int kGuardBuckets = 8;

/** Sliding maximum of |x| over a window, implemented as a ring of block
    maxima. O(1) per sample, no allocation, no drift. */
class SlidingPeak
{
public:
    /** The ring always covers at least windowSamples samples, never fewer, so
        a sample that entered the window is guaranteed to still be counted
        when it leaves the lookahead delay. */
    void prepare (int windowSamples, int buckets) noexcept
    {
        bucketCount = std::max (3, buckets);
        bucketLength = std::max (1, (windowSamples + bucketCount - 2) / (bucketCount - 1));
        values.assign ((std::size_t) bucketCount, 0.0f);
        index = 0;
        counter = 0;
    }

    void reset() noexcept
    {
        std::fill (values.begin(), values.end(), 0.0f);
        index = 0;
        counter = 0;
    }

    void push (float magnitude) noexcept
    {
        values[(std::size_t) index] = std::max (values[(std::size_t) index], magnitude);
        if (++counter >= bucketLength)
        {
            counter = 0;
            index = (index + 1) % bucketCount;
            values[(std::size_t) index] = 0.0f;
        }
    }

    float peak() const noexcept
    {
        float worst = 0.0f;
        for (float v : values)
            worst = std::max (worst, v);
        return worst;
    }

    int window() const noexcept { return bucketCount * bucketLength; }

private:
    std::vector<float> values;
    int bucketCount = 8;
    int bucketLength = 1;
    int index = 0;
    int counter = 0;
};

class FreePocketEngine
{
public:
    void prepare (double sampleRateIn, int maximumBlockSize)
    {
        sampleRate = std::max (8000.0, sampleRateIn);
        (void) maximumBlockSize;

        const double rateScale = sampleRate / 48000.0;
        lookahead = std::max (32, (int) std::lround (kLookaheadSamplesAt48k * rateScale));

        for (auto& bank : banks)
            bank.prepare (sampleRate);

        const int selectionWindow = (int) std::lround (0.25 * sampleRate);
        for (auto& p : candidatePeaks)
            p.prepare (selectionWindow, 8);
        dryWindowPeak.prepare (selectionWindow, 8);

        guardWet.prepare (lookahead, kGuardBuckets);
        guardDry.prepare (lookahead, kGuardBuckets);
        ceilingPeak.prepare ((int) std::lround (1.0 * sampleRate), 8);
        delayLength = lookahead + 1;
        for (auto& d : delay)
            d.assign ((std::size_t) delayLength, 0.0f);
        for (auto& d : dryDelay)
            d.assign ((std::size_t) delayLength, 0.0f);
        delayIndex = 0;

        gainSmoother.reset (sampleRate, 250.0f, 1.0f);
        crossfade = 1.0f;
        crossfadeStep = 1.0f / std::max (1.0f, (float) (0.12 * sampleRate));
        dwellSamples = (int) std::lround (0.75 * sampleRate);
        sinceSwitch = dwellSamples;

        activeCurve = previousCurve = 0;
        meterBlock = std::max (1, (int) std::lround (sampleRate / 400.0));
        meterCounter = 0;
        meterDry = meterWet = 0.0f;
        frame = EngineFrame{};
        reset();
    }

    void reset()
    {
        for (auto& bank : banks)
            bank.reset();
        for (auto& p : candidatePeaks)
            p.reset();
        dryWindowPeak.reset();
        guardWet.reset();
        guardDry.reset();
        ceilingPeak.reset();
        for (auto& d : delay)
            std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : dryDelay)
            std::fill (d.begin(), d.end(), 0.0f);
        delayIndex = 0;
        gainSmoother.snapTo (1.0f);
        crossfade = 1.0f;
        sinceSwitch = dwellSamples;
        activeCurve = previousCurve = 0;
        headroomDb = 0.0f;
    }

    void setParameters (const Parameters& p) noexcept
    {
        parameters = p;
        const float character = clamp01 (p.character * 0.01f);
        if (std::abs (character - appliedCharacter) > 1.0e-4f)
        {
            appliedCharacter = character;
            for (auto& bank : banks)
                bank.setCharacter (character);
        }
    }

    int latencySamples() const noexcept { return lookahead; }

    /** In place stereo (or mono) processing. */
    void process (float* left, float* right, int numSamples)
    {
        const float headroomAmount = clamp01 (parameters.headroom * 0.01f);
        const float outputGain = dbToGain (parameters.outputDb);
        const bool stereo = right != nullptr;

        std::array<float, kNumCurves> outL {};
        std::array<float, kNumCurves> outR {};

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = sanitise (left[n]);
            const float dryR = stereo ? sanitise (right[n]) : dryL;

            banks[0].process (dryL, outL.data());
            if (stereo)
                banks[1].process (dryR, outR.data());

            // --- candidate measurement -------------------------------------
            const float dryMagnitude = std::max (std::abs (dryL), std::abs (dryR));
            dryWindowPeak.push (dryMagnitude);
            for (int c = 0; c < kNumCurves; ++c)
            {
                const float magnitude = stereo ? std::max (std::abs (outL[(std::size_t) c]),
                                                           std::abs (outR[(std::size_t) c]))
                                               : std::abs (outL[(std::size_t) c]);
                candidatePeaks[(std::size_t) c].push (magnitude);
            }

            if (++sinceSwitch >= dwellSamples && crossfade >= 1.0f)
                considerSwitch (headroomAmount);

            // --- crossfade between the previous and the active curve -------
            float wetL = outL[(std::size_t) activeCurve];
            float wetR = stereo ? outR[(std::size_t) activeCurve] : 0.0f;

            if (crossfade < 1.0f)
            {
                crossfade = std::min (1.0f, crossfade + crossfadeStep);
                const float t = crossfade;
                wetL = lerp (outL[(std::size_t) previousCurve], wetL, t);
                if (stereo)
                    wetR = lerp (outR[(std::size_t) previousCurve], wetR, t);
                if (crossfade >= 1.0f)
                    previousCurve = activeCurve;
            }

            // --- lookahead guard -------------------------------------------
            const float wetMagnitude = stereo ? std::max (std::abs (wetL), std::abs (wetR))
                                              : std::abs (wetL);
            guardWet.push (wetMagnitude);
            guardDry.push (dryMagnitude);
            ceilingPeak.push (dryMagnitude);

            delay[0][(std::size_t) delayIndex] = wetL;
            delay[1][(std::size_t) delayIndex] = wetR;
            dryDelay[0][(std::size_t) delayIndex] = dryL;
            dryDelay[1][(std::size_t) delayIndex] = dryR;
            const int readIndex = (delayIndex + 1) % delayLength;
            delayIndex = readIndex;

            const float delayedL = delay[0][(std::size_t) readIndex];
            const float delayedR = delay[1][(std::size_t) readIndex];
            const float delayedDryL = dryDelay[0][(std::size_t) readIndex];
            const float delayedDryR = dryDelay[1][(std::size_t) readIndex];
            const float delayedDryMagnitude = std::max (std::abs (delayedDryL), std::abs (delayedDryR));

            // The guard compares the same window of the dry and of the rotated
            // signal. When no rotation is active the two are identical and the
            // allowed gain is exactly 1, which keeps Headroom = 0 % bit exact.
            const float lookaheadWet = guardWet.peak();
            const float lookaheadDry = guardDry.peak();
            // The ceiling is the dry peak of the last second, never a local
            // dip, so quiet passages are not gated down. It can still never
            // exceed a level the dry signal actually reached.
            const float ceiling = std::max (ceilingPeak.peak(), lookaheadDry);
            const float allowedGain = lookaheadWet > 1.0e-9f ? ceiling / lookaheadWet : 1.0f;

            const float targetGain = dbToGain (headroomDb * headroomAmount);
            gainSmoother.setTarget (std::min (targetGain, allowedGain));
            const float smoothed = gainSmoother.next();
            const float gain = std::min (smoothed, allowedGain);

            const float finalL = sanitise (delayedL * gain * outputGain);
            const float finalR = sanitise (delayedR * gain * outputGain);

            left[n] = finalL;
            if (stereo)
                right[n] = finalR;

            // --- metering ---------------------------------------------------
            meterDry = std::max (meterDry, delayedDryMagnitude);
            meterWet = std::max (meterWet, std::max (std::abs (finalL), std::abs (finalR)));
            if (++meterCounter >= meterBlock)
            {
                frame.dryPeak = meterDry;
                frame.wetPeak = meterWet;
                frame.gainDb = gainToDb (gain);
                frame.headroomDb = headroomDb;
                frame.curve = activeCurve;
                meterReady = true;
                meterCounter = 0;
                meterDry = meterWet = 0.0f;
            }
        }
    }

    bool popFrame (EngineFrame& destination) noexcept
    {
        if (! meterReady)
            return false;
        destination = frame;
        meterReady = false;
        return true;
    }

    const EngineFrame& lastFrame() const noexcept { return frame; }
    int currentCurve() const noexcept { return activeCurve; }
    float currentHeadroomDb() const noexcept { return headroomDb; }

private:
    void considerSwitch (float headroomAmount) noexcept
    {
        const float dryPeak = std::max (dryWindowPeak.peak(), 1.0e-7f);
        if (dryPeak < 1.0e-5f)
            return;

        int best = 0;
        float bestPeak = candidatePeaks[0].peak();
        const int allowed = allowedCurveCount (headroomAmount);

        for (int c = 1; c < allowed; ++c)
        {
            const float p = candidatePeaks[(std::size_t) c].peak();
            if (p < bestPeak)
            {
                best = c;
                bestPeak = p;
            }
        }

        const float currentPeak = std::max (candidatePeaks[(std::size_t) activeCurve].peak(), 1.0e-9f);
        const float improvement = gainToDb (currentPeak / std::max (bestPeak, 1.0e-9f));

        if (best != activeCurve && improvement > 0.25f)
        {
            previousCurve = activeCurve;
            activeCurve = best;
            crossfade = 0.0f;
            sinceSwitch = 0;
        }

        // headroom actually recovered by the curve that is running now
        const float running = std::max (candidatePeaks[(std::size_t) activeCurve].peak(), 1.0e-9f);
        const float measured = gainToDb (dryPeak / running);
        headroomDb = clampf (measured, 0.0f, 12.0f);
    }

    /** The Headroom knob also gates how adventurous the rotation may be, so a
        low setting keeps the phase rotation shallow and strictly low band. */
    int allowedCurveCount (float headroomAmount) const noexcept
    {
        if (headroomAmount <= 0.001f)
            return 1;
        return 2 + (int) std::lround (headroomAmount * (float) (kNumCurves - 2));
    }

    Parameters parameters {};
    DispersionBank banks[2];
    SlidingPeak candidatePeaks[kNumCurves];
    SlidingPeak dryWindowPeak;
    SlidingPeak guardWet;
    SlidingPeak guardDry;
    SlidingPeak ceilingPeak;
    int delayLength = kLookaheadSamplesAt48k + 1;
    std::vector<float> delay[2];
    std::vector<float> dryDelay[2];

    double sampleRate = 48000.0;
    int lookahead = kLookaheadSamplesAt48k;
    int delayIndex = 0;

    Smoother gainSmoother;
    float headroomDb = 0.0f;
    float appliedCharacter = -1.0f;

    int activeCurve = 0;
    int previousCurve = 0;
    float crossfade = 1.0f;
    float crossfadeStep = 0.001f;
    int dwellSamples = 36000;
    int sinceSwitch = 0;

    int meterBlock = 120;
    int meterCounter = 0;
    float meterDry = 0.0f;
    float meterWet = 0.0f;
    bool meterReady = false;
    EngineFrame frame {};
};

} // namespace fp
