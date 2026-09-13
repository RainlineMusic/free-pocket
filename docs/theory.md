# Free Pocket: theory, formulas and measurements

## 1. The idea

A peak is not only a matter of level, it is a matter of *phase alignment*. For
a signal built from N partials

```
x(t) = sum_k A_k cos(2 pi f_k t + phi_k)
```

the RMS depends only on the amplitudes,

```
RMS^2 = (1/2) sum_k A_k^2,
```

while the peak depends on the phases `phi_k`. With all phases equal (the
"zero-phase" case) the crest factor grows like `sqrt(2N)`; with well-chosen
phases it can be pushed towards about 1.4 (Boyd, 1986). Real music sits
somewhere between: bass notes with in-phase harmonics, dense limited mixes and
synth stacks all carry avoidable crest factor.

An **allpass filter** changes `phi_k` and nothing else:

```
|H(e^{j w})| = 1 for all w   =>   spectrum unchanged, RMS unchanged
```

So phase rotation is the only peak-reduction mechanism that is, by
construction, spectrally free. Every other one (clipping, limiting, saturation)
pays with distortion or gain modulation.

## 2. The filter

Second-order RBJ allpass section:

```
H(z) = (c + d z^-1 + z^-2) / (1 + d z^-1 + c z^-2)

w0    = 2 pi fc / fs
alpha = sin(w0) / (2 Q)
c     = (1 - alpha) / (1 + alpha)
d     = -2 cos(w0) / (1 + alpha)
```

Direct-form difference equation used in the plugin:

```
y[n] = c x[n] + d x[n-1] + x[n-2] - d y[n-1] - c y[n-2]
```

Phase and group delay of one section:

```
phi(w)   = -2 atan2( (1 - c) sin w , ... )        (evaluated numerically)
tau(w)   = -d phi / d w
```

A curve is a cascade of up to 6 sections with corner frequencies spread
logarithmically between a low and a high edge. `Character` scales that high
edge: `scale = 0.28 + 0.72 * character`, with the highest corner never below
60 Hz.

## 3. Why a fixed rotator is not enough

Prototype round 2 (`SR = 48 kHz`, Q = 0.5) confirmed the allpass property
analytically (magnitude error 1.07e-11 ... 1.75e-11 dB, measured <= 1.18e-09 dB)
and then measured the peak change of a *fixed* cascade on real material:

| Signal | 500 Hz, n = 8 | best fixed set (20 Hz, 1 kHz, n = 4) |
| --- | --- | --- |
| impulse | -3.98 dB | - |
| kick (55 Hz) | -2.25 dB | - |
| bass pluck (41.2 Hz) | -2.27 dB | - |
| snare | -0.50 dB | - |
| zero-phase multitone | -1.76 dB | - |
| full mix | **+2.38 dB (worse)** | -1.94 dB worst case |

Negative means the peak went *up*. A single fixed rotation is a gamble: it wins
on some material and loses badly on other. That is why Free Pocket is adaptive.

## 4. The adaptive bank

Round 3 compared three strategies:

| Strategy | kick | bass | snare | mix | edm |
| --- | --- | --- | --- | --- | --- |
| A: per-signal optimum (oracle) | +0.46 | +1.69 | +3.62 | +3.76 | +0.97 dB |
| B: adaptive bank + identity fallback | +0.28 | +0.31 | +2.97 | **+3.02** | +0.85 dB |

Strategy B reaches most of the oracle gain **and its worst block is +0.00 dB**:
it can never make the peak worse, because the identity curve is always one of
the candidates. Block-size sweep: the same +3.02 dB at 0.25 s, 0.5 s and 1 s
windows, so the fastest window was chosen.

Curve set shipped (sections, high corner, Q = 0.5):

```
0: identity
1: 3 sections, 150 Hz
2: 6 sections, 260 Hz
3: 3 sections, 700 Hz
4: 6 sections, 1500 Hz
5: 4 sections, 4000 Hz
```

Selection rule, per 0.25 s window:

```
best       = argmin_c  peak_window(c)
switch if  20 log10( peak(active) / peak(best) ) > 0.25 dB
           and at least 0.75 s since the last switch
crossfade  120 ms, linear
headroomDb = clamp( 20 log10( peak_dry / peak_active ), 0, 12 )
```

The number of candidates is gated by the Free knob:
`allowedCurves(h) = 1 if h <= 0.1 %, else 2 + round(h * (6 - 2))`, so Free = 0 %
is literally a bit-exact bypass.

## 5. The structural peak guard

The makeup stage is not a limiter. With a 256-sample lookahead (at 48 kHz) the
engine knows the wet peak of the window before it plays it:

```
ceiling      = max( sliding 1 s dry peak , lookahead dry peak )
allowedGain  = ceiling / lookahead wet peak
targetGain   = 10^( headroomDb * freeAmount / 20 )
gain         = min( smoothed(targetGain), allowedGain )
```

`gain <= allowedGain` makes `output peak <= input peak` an invariant of the
structure, not a behaviour of a detector. The 1 s ceiling keeps quiet passages
from being gated down.

## 6. Measured results (Tests/dsp_core_test.cpp, 24 checks, all passing)

| Measurement | Result |
| --- | --- |
| max deviation of `\|H\|` from unity | 0.0064 dB |
| RMS ratio through the cascade | 0.99985 |
| group delay 500 Hz / 1 k / 2 k / 4 k | 2.062 / 0.892 / 0.329 / 0.101 ms |
| 99 % of impulse energy inside | 4.6 ms |
| peak ratio out/in, multitone / kick / hot noise | 0.99987 / 0.99606 / 0.999999 |
| kick RMS change | **+3.535 dB** |
| dense multitone free level | **+3.22 dB at +0.0000006 dB peak change** |
| Free = 0 % vs delayed dry | 0 (bit exact) |
| L vs R on identical input | 0 |
| inter-channel correlation | 0.7245 -> 0.7248 |
| output trim -6 dB | exact |

The 4 kHz group delay of 0.101 ms sits an order of magnitude under the 0.64 ms
audibility threshold reported by Liski et al.; 500 Hz at 2.06 ms is above their
threshold for that band, which is why `Character` defaults to 40 % and the low
curves are the ones used first.

## 7. References

- Liski, Makivirta, Valimaki, *Audibility of Group-Delay Equalization*,
  IEEE/ACM TASLP, 2021 -
  https://acris.aalto.fi/ws/portalfiles/portal/66449704/Audibility_of_Group_Delay_Equalization.pdf
- Boyd, *Multitone signals with low crest factor*, IEEE TCAS, 1986 -
  https://web.stanford.edu/~boyd/papers/pdf/multitone_low_crest.pdf
- Smith, *Allpass Filters* and *Group Delay*, CCRMA -
  https://ccrma.stanford.edu/~jos/pasp/Allpass_Filters.html ,
  https://ccrma.stanford.edu/~jos/fp/Group_Delay.html
- Universal Audio, *Allpass filters and the phase rotator* -
  https://www.uaudio.com/blogs/ua/allpass-filters
- Wang and Luo, *Crest factor reduction by iterative clipping and filtering* -
  https://web.xidian.edu.cn/ychwang/files/20111209_164923.pdf
- Zhu et al., *Peak cancellation crest factor reduction*, IEEE, 2013 -
  https://ieeexplore.ieee.org/document/6469001/
