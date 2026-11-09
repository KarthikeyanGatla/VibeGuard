# VibeGuard test report

**Date:** 5 Nov 2026 · **Firmware:** `firmware/VibeGuard` with hysteresis · **Result:** 15/15 host checks pass. Both Wokwi runs behaved as expected after one bug was found and fixed.

| Test level | What was run | Outcome |
|---|---|---|
| Host tests | Firmware DSP and diagnosis code, compiled with clang++, 15 checks, about 3 h of simulated running | **15 pass, 0 fail** |
| Wokwi run 1 | Full firmware on a simulated ESP32 + MPU6050: calibrate, imbalance, looseness, bearing, motor off, recovery | All faults identified correctly. **Found a WARNING/ALARM flicker bug.** |
| Wokwi run 2 | Same firmware with the fix: calibrate, then 44 s of bearing fault | Flicker gone (0 state changes) |
| Dashboard | Real page served by the mock with replayed engine output, at desktop width and at 375 px | Renders correctly, controls work, no horizontal scroll at phone width |
| Real hardware | Not done | Needs a physical ESP32, MPU6050 and motor |

Raw data: `tests/results/summary.md` (regenerated on each run), `shaft_estimate.csv`, `severity_sweep.csv` and `wokwi_serial_log.txt`.

## Test signal (simulated motor)

| Component | Healthy | Fault at severity 1.0 |
|---|---|---|
| 1× at 25 Hz (1500 RPM) | 40 mg | Imbalance: 120 mg (×3) |
| 2× / 3× | 8 / 4 mg | Looseness: 40 / 28 mg, plus a 0.5× sub-harmonic and 4×–6× lines |
| 150–210 Hz resonance, modulated at BPFO = 3.05 × f1 | 2 mg | Bearing: 18 mg |
| White noise (MPU6050 floor) | 4 mg rms | Same |
| Quantisation | ±4 g, 8192 LSB/g, plus 1 g of gravity | Same |

## 1. FFT and window

| Check | Measured |
|---|---|
| FFT vs. direct DFT on random data | Max relative power error 1.5 × 10⁻⁶ |
| 50 mg tone on a bin centre | 50.00 mg recovered |
| Tone half-way between bins (worst case) | 42.4 mg peak, a −1.42 dB scalloping loss. The ±2-bin band still holds **99.9%** of the energy, so band energies are unaffected. |
| Resolution / frame / Nyquist | 1.953 Hz per bin, 0.512 s, 250 Hz |

## 2. Shaft speed (1×) estimate

There were 31 speeds from 8.5 to 78 Hz (510–4680 RPM).

| Mean absolute error | Worst error | Peaks not found |
|---|---|---|
| **0.028 Hz** (1.7 RPM) | 0.29 Hz (17 RPM) | 0 |

On Wokwi, the true speed was 25.00 Hz and the device learned **24.98 Hz (1499 RPM)**.

## 3. Fault classification

There were 20 random seeds per condition, judged 15.4 s after the fault started.

| Simulated \ Verdict | Healthy | Imbalance | Looseness | Bearing | Motor off | Mean health |
|---|---|---|---|---|---|---|
| Healthy | **20** | 0 | 0 | 0 | 0 | 98.5% |
| Imbalance | 0 | **20** | 0 | 0 | 0 | 20.8% |
| Looseness | 0 | 0 | **20** | 0 | 0 | 0.0% |
| Bearing | 0 | 0 | 0 | **20** | 0 | 32.8% |
| Motor off | 0 | 0 | 0 | 0 | **20** | n/a |

**100/100 correct.** Detection latency:

| Fault | Host test | Wokwi run 1 |
|---|---|---|
| Imbalance | 1.5 s | ≈ 2 s to ALARM |
| Looseness | 1.5 s | ≈ 2 s to ALARM (it was replacing an imbalance alarm) |
| Bearing | 2.0 s | ≈ 2 s to WARNING, ≈ 4 s to ALARM |

## 4. False alarms

There were 10 seeds × 400 frames per case, about 34 minutes of running each.

| Case | Non-healthy frames | Lowest health |
|---|---|---|
| Same speed as learned | **0 / 4000** | 90.5% |
| Speed +5% | **0 / 4000** | 90.1% |
| Speed −5% | **0 / 4000** | 90.3% |
| Speed +9% | **0 / 4000** | 90.1% |

## 5. Sensitivity (smallest fault detected)

| Fault | First WARNING | First ALARM | Health at severity 0.25 / 0.5 / 1.0 / 1.5 |
|---|---|---|---|
| Imbalance | 1× amplitude ×1.8 | ×2.5 | 71 / 50 / 21 / 0% |
| Looseness | 2× amplitude ×1.8 | ×2.4 | 47 / 17 / 0 / 0% |
| Bearing | Resonance ×5.4 (≈ 11 mg) | ×8.6 (≈ 17 mg) | 83 / 64 / 32 / 9% |

The bearing band is the least sensitive, because its energy is spread across 70 bins of noise. In practice a bearing is flagged once its defect vibration reaches about 3× the sensor noise floor.

## 6. Edge cases

| Check | Result |
|---|---|
| Motor stopped during calibration | 10 stopped frames rejected. Learning still used 39 running frames (19.97 s). |
| Motor stops, then restarts | MOTOR_OFF, then HEALTHY, with no false fault |
| Fault repaired | Back to HEALTHY in 2.0 s (Wokwi: about 2 s from motor-on to health above 80%) |
| **State flicker at the threshold** | **0 changes in 10 × 100 frames** (it was 56 before the fix) |
| 270 Hz input (above Nyquist) | Aliases to 230.5 Hz. This is a documented limitation that the MPU6050's 260 Hz DLPF must handle. |
| Sensor resolution | 0.122 mg/LSB. The healthy 1× line is 327 LSB. |

## 7. Timing and resources

| Quantity | Host (Apple Silicon) | Simulated ESP32 (Wokwi) | Budget |
|---|---|---|---|
| `processFrame` while learning | n/a | 3.2–3.7 ms | 512 ms per frame |
| `processFrame` while monitoring | 1.5 µs | **5.0–5.5 ms** (about 1% of the budget) | 512 ms per frame |
| Frame overruns | n/a | 0 | n/a |
| I2C errors | n/a | 0 | n/a |
| Worst sample-timer jitter since boot | n/a | 9.3 ms (identical in both runs, most likely once during WiFi start-up) | 2 ms period |
| Engine RAM | 26.7 KB | Same | 520 KB SRAM |

Wokwi's timings are approximate. On real hardware, read `rt.procUs` and `rt.jitterUs` from `/api/status`.

## Bug found and fixed

**Symptom (Wokwi run 1):** at nominal bearing severity, the smoothed bearing ratio hovered around 6.0, which is the alarm threshold. The state toggled WARNING ↔ ALARM 16 times in 46 s, so the LED switched between 1 Hz and 5 Hz blinking and the dashboard badge changed colour.

**Fix:** hysteresis in `vg_diagnosis.h`, configured in `vg_config.h`.
- Once declared, a fault stays active until its ratio drops below **0.8 ×** the warning threshold (`CLEAR_HYSTERESIS`).
- Once in ALARM, the state drops to WARNING only below **0.85 ×** the alarm threshold (`ALARM_HYSTERESIS`).

**Verification:** new regression test "No WARNING/ALARM flicker at the threshold" (56 changes before, 0 after). Wokwi run 2 had 0 changes over 49 lines. Classification, latency and false-alarm results are unchanged. Recovery after repair went from 1.5 s to 2.0 s.

## Not yet verified

- **Real hardware.** Sensor noise, mounting, mains-frequency pickup and real fault signatures will differ from the simulator. Do a bench test with a small motor plus a deliberately added imbalance (a blob of putty on the fan), loosened mounting screws and, if possible, a worn bearing.
- **Dashboard served by the ESP32 itself.** Wokwi's web editor can't forward port 80, so the page was checked through the mock server, which serves the same HTML.
- **Sample-timer jitter at boot.** Check `rt.jitterUs` on hardware. If it is large, reset the statistic after calibration starts.
