# Free Pocket

Part of the **Pocket** series: two knobs, one problem solved at the level of a
serious, expensive box.

Free Pocket buys you peak headroom **without touching the magnitude spectrum
and without a limiter**. It rotates the phase of the signal with an adaptive
allpass bank, which changes only *when* the partials arrive, not *how loud*
they are. On material whose partials line up in phase (bass with in-phase
harmonics, dense mixes, synth stacks) that alignment is exactly what makes the
peak, and rotating it recovers 1-3 dB of true peak room. The recovered room is
then handed back as level: same spectrum, same peak, more loudness.

## Controls

| Control | What it does |
| --- | --- |
| **Free** | How much of the recovered peak room is handed back as level. At 0 % the plugin is a bit-exact, latency-compensated bypass. |
| **Character** | How far up the spectrum the rotation is allowed to reach. Low settings keep it in the bass, where group delay is inaudible. |
| **Output** | Output trim, -24 to +12 dB. |

The display on the left is a Pro-L2 style peak history: the input peak in one
colour, the processed peak on top of it in another. The gap between the two is
the headroom the rotation recovered.

## Guarantees, enforced by tests

1. The dispersion is a true allpass: magnitude spectrum and RMS survive it
   (measured deviation 0.0064 dB).
2. Group delay stays inside the published audibility budget (Liski, Makivirta,
   Valimaki, IEEE/ACM TASLP 2021).
3. The output peak can never exceed the input peak, structurally, not by
   limiting.
4. Loudness is never lost, even on material that dislikes rotation (a fixed
   rotator costs a kick about 2 dB - this one falls back to identity instead).
5. Free at 0 % is sample-exact bypass.
6. L and R always get identical processing, so the stereo image cannot move.

See [`docs/theory.md`](docs/theory.md) for the derivation, the prototype
measurements and the references.

## Building

CI builds VST3 and AAX for macOS (universal) and Windows on every push to a
`feature/**` branch. Locally:

```bash
cmake -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Without JUCE present only the DSP test target is configured, which is enough
to verify the core:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o fptests Tests/dsp_core_test.cpp && ./fptests
```

(c) 2026 Rainline Music. All Rights Reserved.
