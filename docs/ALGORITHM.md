# Algorithm: from vibration to a diagnosis

All of this is in [`vg_diagnosis.h`](../firmware/VibeGuard/vg_diagnosis.h) and [`vg_dsp.h`](../firmware/VibeGuard/vg_dsp.h), and every number is set in [`vg_config.h`](../firmware/VibeGuard/vg_config.h). The host tests compile these exact files, so what is tested is what runs on the ESP32.

## Signal chain (one frame, every 0.512 s)

1. **Sample.** 256 Z-axis readings at 500 Hz, quantised at 8192 LSB/g.
2. **Remove DC.** Subtract the frame mean, which removes gravity (≈ 1 g) and sensor offset. Compute the RMS.
3. **Motor-off check.** If RMS < 10 mg, the frame is *MOTOR_OFF*. It's skipped during learning and doesn't trigger faults.
4. **Hann window**, then a **256-point radix-2 FFT**, then the power |X[k]|² for bins 0–127. Bin k is k × 1.953 Hz.
5. **Find 1× (shaft speed).** Search for the peak within ±10% of the learned shaft frequency, refined with parabolic interpolation (sub-bin accuracy). If no clear peak exists, use the learned value.
6. **Band energies.**
   | Band | Bins summed | Fault it indicates |
   |---|---|---|
   | 1× | round(f1/Δf) ± 2 | Rotor **imbalance** |
   | 2× + 3× | round(2f1/Δf) ± 2 and round(3f1/Δf) ± 2 | Mechanical **looseness** |
   | 100–240 Hz | bins 52–122, minus ±1 bin around 1×…8× harmonics | **Bearing** wear |
7. **Ratio to baseline.** r = (E + ε) / (E_baseline + ε), smoothed with an EMA (α = 0.3).
8. **Decision.** A band is *active* once its smoothed ratio has stayed at or above its warning threshold for 3 frames in a row (1.5 s). The fault is the active band with the highest ratio relative to its threshold. *ALARM* if that ratio reaches the alarm threshold, otherwise *WARNING*. **Hysteresis:** an active band clears only below 0.8 × warning, and an alarm drops to warning only below 0.85 × alarm. Without it, a fault sitting right at a threshold made the state flicker (found in Wokwi, see TEST_REPORT.md).
9. **Health %** = 100 × (1 − ln(r_max) / ln 16), clamped to 0–100. Ratio 1 → 100%, ratio 16 (+12 dB) → 0%.
10. **Advice.** Each fault maps to an action, a repair cost and the cost of a burnt motor (₹1,800). The dashboard shows the saving.

## Baseline learning (first 20 s, or after "Re-learn")

- 39 frames are stored (39 × 128 floats = 20 KB). Frames with the motor off are rejected and don't count.
- Averaging their spectra gives the learned **shaft frequency**: the strongest peak in 8–80 Hz with interpolation. It is flagged `shaftFound=false` if the peak is not 4× the mean of the range.
- For each band, the per-frame energies give a **mean** (the baseline) and a **coefficient of variation** (cv).
- **Thresholds are learned for each motor:**
  - warning = max(3.0, 1 + 4 × cv)
  - alarm = max(6.0, warning × 2)

  A noisy motor gets wider thresholds automatically. That answers "the thresholds were tuned on our motor": the 3.0/6.0 floors are the only fixed numbers, and they are energy ratios (+4.8 dB / +7.8 dB), not absolute g values.

## Answers to the questions flagged before the presentation

**"How do you find the 1× shaft frequency?"**
Peak search, not a fixed range. During learning, the strongest spectral line between 8 and 80 Hz in the averaged spectrum is taken as 1×, refined to about 0.03 Hz by fitting a parabola to the log power of the peak bin and its neighbours. During monitoring it is re-tracked within ±10%, so a 5–9% speed change doesn't create a false alarm. Tests: mean error 0.028 Hz and worst 0.29 Hz over 31 speeds; zero false alarms at ±5% and +9% speed drift.

**"Your bearing band ends at 240 Hz but Nyquist is 250 Hz?"**
Correct, and it's deliberate. 240 Hz is bin 122 of 127, so the band stays below Nyquist with a 5-bin guard. The real risk is *aliasing*: anything above 250 Hz folds back (a 270 Hz component appears at 230 Hz, see test 6). The MPU6050's DLPF at 260 Hz attenuates most of that, but not perfectly. The honest fix is a higher sample rate, which would mean `FS_HZ = 1000` with a 512-point FFT (same 1.95 Hz resolution, 500 Hz Nyquist). The ESP32 has the CPU budget for that. The cost is twice the I2C traffic and memory.

**"Does the dashboard actually show repair cost vs failure cost?"**
Yes. `/api/status` carries `advice.action`, `advice.repair`, `advice.failure` and `advice.saving`, and the dashboard's Advice card shows them. The bearing and motor figures (₹150 and ₹1,800) are from the project pitch. The re-balance (₹100) and tighten (₹50) figures are estimates in `vg_config.h`.

**"Are the thresholds hard-coded?"**
Only their floors are. See *Baseline learning* above.

## Why these choices

| Choice | Reason |
|---|---|
| Hann window | Low leakage. A tone half-way between bins still keeps 99.9% of its energy inside the ±2-bin band (test 1). The scalloping loss is 1.42 dB. |
| Energy ratios, not absolute g | Works on any motor and any mounting without per-motor tuning |
| EMA + 3-frame persistence | A single knock or spike can't raise a fault. Detection still takes 1.5–2 s. |
| Harmonic lines removed from the bearing band | Looseness produces 4×–8× lines that would otherwise also raise the bearing ratio |
| Re-tracking 1× within ±10% | Mains-powered motors slip with load. DC motors drift with supply voltage. |
| Z axis only | The radial direction carries imbalance and bearing energy, and one axis keeps I2C at 2 bytes per sample |

## Known limitations

- Fault signatures are idealised in the simulator. Real looseness can show half-harmonics, and real bearings show sidebands. Both still raise the bands VibeGuard watches, but the ratios will differ.
- Two simultaneous faults are reported as the stronger one.
- The bearing band can't see defects above 250 Hz (the sample-rate limit above).
- If the motor already has a fault while the baseline is being learned, that fault becomes "normal". Learn on a known-good motor.
