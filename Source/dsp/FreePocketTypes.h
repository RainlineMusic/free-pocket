/*
    Free Pocket - shared DSP types and helpers.
    Copyright (c) 2026 Rainline Music. All Rights Reserved.

    JUCE free on purpose: the whole DSP core compiles and runs with a bare
    C++17 compiler, both locally and in CI.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace fp
{

inline constexpr double kPi = 3.14159265358979323846;

/** User-facing parameter set, in user units. */
struct Parameters
{
    float headroom = 60.0f;   // 0 .. 100 %, how much of the recovered peak room to take as gain
    float character = 40.0f;  // 0 .. 100 %, how far up the spectrum the phase may be rotated
    float outputDb = 0.0f;    // -24 .. +12 dB
};

/** Lock-free snapshot handed to the UI. Never read by the DSP. */
struct EngineFrame
{
    float dryPeak = 0.0f;      // linear, per meter block
    float wetPeak = 0.0f;      // linear, per meter block, after gain
    float gainDb = 0.0f;       // makeup gain currently applied
    float headroomDb = 0.0f;   // peak room the dispersion has actually recovered
    int   curve = 0;           // which dispersion curve is active, 0 = none
};

inline float clampf (float value, float low, float high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

inline float clamp01 (float value) noexcept
{
    return clampf (value, 0.0f, 1.0f);
}

inline float dbToGain (float db) noexcept
{
    return std::pow (10.0f, db * 0.05f);
}

inline float gainToDb (float gain) noexcept
{
    return 20.0f * std::log10 (std::max (gain, 1.0e-9f));
}

inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

/** Replaces NaN and Inf with zero. The final real-time safety net. */
inline float sanitise (float value) noexcept
{
    return std::isfinite (value) ? value : 0.0f;
}

/** One-pole smoother, time constant in milliseconds. */
class Smoother
{
public:
    void reset (double sampleRate, float timeMs, float initialValue = 0.0f) noexcept
    {
        setTime (sampleRate, timeMs);
        current = target = initialValue;
    }

    void setTime (double sampleRate, float timeMs) noexcept
    {
        const double tau = std::max (0.01, (double) timeMs) * 0.001;
        coefficient = (float) (1.0 - std::exp (-1.0 / (std::max (1.0, sampleRate) * tau)));
    }

    void setTarget (float value) noexcept { target = value; }
    void snapTo (float value) noexcept { target = current = value; }

    float next() noexcept
    {
        current += coefficient * (target - current);
        return current;
    }

    float value() const noexcept { return current; }

private:
    float coefficient = 1.0f;
    float current = 0.0f;
    float target = 0.0f;
};

} // namespace fp
