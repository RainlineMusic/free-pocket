/*
    Free Pocket - allpass dispersion bank.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.

    Theory
    ------
    A second order allpass section (RBJ cookbook) has the transfer function

        H(z) = (c + d z^-1 + z^-2) / (1 + d z^-1 + c z^-2)

        w0    = 2 pi fc / fs
        alpha = sin(w0) / (2 Q)
        c     = (1 - alpha) / (1 + alpha)
        d     = -2 cos(w0) / (1 + alpha)

    |H(e^jw)| = 1 for every w, exactly, because the numerator is the mirrored
    (reversed and conjugated) denominator. Cascading sections therefore leaves
    the magnitude spectrum untouched while redistributing phase, which moves
    energy in time and changes the crest factor of the waveform without
    changing its spectrum or its RMS.

    Group delay of one section:

        tau(w) = (1 - c^2) / (1 + 2 c cos(w + phi) + c^2)  ... evaluated
                 numerically in the tests from the phase derivative.

    Sections are spread logarithmically between 25 Hz and an upper corner. The
    upper corner decides where the phase rotation stops, which is what the
    Character knob controls.
*/

#pragma once

#include "FreePocketTypes.h"

namespace fp
{

/** One second order allpass section, direct form 1, double state. */
class AllpassSection
{
public:
    void setFrequency (double frequencyHz, double sampleRate, double q = 0.5) noexcept
    {
        const double nyquist = sampleRate * 0.5;
        const double fc = std::min (std::max (frequencyHz, 5.0), nyquist * 0.95);
        const double w0 = 2.0 * kPi * fc / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * std::max (0.05, q));

        c = (float) ((1.0 - alpha) / (1.0 + alpha));
        d = (float) (-2.0 * std::cos (w0) / (1.0 + alpha));
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = c * x + d * x1 + x2 - d * y1 - c * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = sanitise (y);
        return y1;
    }

private:
    float c = 0.0f, d = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

inline constexpr int kMaxSections = 6;

/** A cascade of up to kMaxSections allpass sections: one dispersion curve. */
class DispersionCurve
{
public:
    void configure (int sectionCount, double lowHz, double highHz, double q, double sampleRate) noexcept
    {
        count = std::min (sectionCount, kMaxSections);
        for (int i = 0; i < count; ++i)
        {
            const double t = count == 1 ? 0.5 : (double) i / (double) (count - 1);
            const double f = lowHz * std::pow (highHz / lowHz, t);
            sections[i].setFrequency (f, sampleRate, q);
        }
        for (int i = count; i < kMaxSections; ++i)
            sections[i].reset();
    }

    void reset() noexcept
    {
        for (auto& s : sections)
            s.reset();
    }

    int sectionCount() const noexcept { return count; }

    float process (float x) noexcept
    {
        for (int i = 0; i < count; ++i)
            x = sections[i].process (x);
        return x;
    }

private:
    AllpassSection sections[kMaxSections];
    int count = 0;
};

/** Layout of the candidate bank. Entry 0 is deliberately the identity, which
    is what makes the "never worse than the dry signal" guarantee structural
    rather than a promise: if no rotation helps, the plugin rotates nothing. */
struct CurveSpec
{
    int sections;
    float highHz;  // upper corner at Character = 100 %
    float q;
};

inline constexpr int kNumCurves = 6;

inline const CurveSpec* curveSpecs() noexcept
{
    static const CurveSpec specs[kNumCurves] = {
        { 0, 0.0f,    0.5f },   // identity
        { 3, 150.0f,  0.5f },
        { 6, 260.0f,  0.5f },
        { 3, 700.0f,  0.5f },
        { 6, 1500.0f, 0.5f },
        { 4, 4000.0f, 0.5f }
    };
    return specs;
}

/** All candidate curves for one channel, run in parallel. */
class DispersionBank
{
public:
    void prepare (double sampleRateIn) noexcept
    {
        sampleRate = sampleRateIn;
        setCharacter (currentCharacter);
        reset();
    }

    void reset() noexcept
    {
        for (auto& curve : curves)
            curve.reset();
    }

    /** character: 0 .. 1. Scales the upper corner of every curve, so at 0 the
        rotation stays in the low end where the ear is least sensitive to
        group delay, and at 1 it reaches into the midrange. */
    void setCharacter (float character) noexcept
    {
        currentCharacter = clamp01 (character);
        const double scale = 0.28 + 0.72 * (double) currentCharacter;
        const CurveSpec* specs = curveSpecs();

        for (int i = 1; i < kNumCurves; ++i)
            curves[i].configure (specs[i].sections, 25.0,
                                 std::max (60.0, specs[i].highHz * scale),
                                 specs[i].q, sampleRate);
    }

    /** Writes the output of every curve for one input sample. out[0] == x. */
    void process (float x, float* out) noexcept
    {
        out[0] = x;
        for (int i = 1; i < kNumCurves; ++i)
            out[i] = curves[i].process (x);
    }

private:
    DispersionCurve curves[kNumCurves];
    double sampleRate = 48000.0;
    float currentCharacter = 0.4f;
};

} // namespace fp
