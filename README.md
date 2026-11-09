# VibeGuard

**Predictive maintenance for rotating machines using vibration analysis and edge processing.**
An ESP32 + MPU6050 device (≈ ₹1,140) that learns a motor's healthy vibration in 20 seconds, then tells you *which* fault is developing (rotor imbalance, mechanical looseness or bearing wear) and what fixing it now saves compared with a burnt motor. Everything runs on the device. The dashboard is served from its own WiFi access point, with no internet, cloud or subscription.

IoT course project, 21CSE253T, SRM Institute of Science and Technology (faculty: Dr. Arulalan V).
Team: Thanvik Sourya Gunupati (RA2311003010972), Yashwanth Lakkakula (RA2311003010969), G. Karthikeyan Reddy (RA2311003010725).

## Status (5 Nov 2026)

| Part | State |
|---|---|
| Firmware (`firmware/VibeGuard/`) | Written from scratch and **runs on a simulated ESP32 in Wokwi** (compiles, MPU6050 detected, learns the baseline, diagnoses injected faults) |
| Diagnosis engine | **15/15 host tests pass**, 100/100 fault runs classified correctly, 0 false alarms in 2.3 h of healthy running; a threshold-flicker bug found in Wokwi was fixed (see [docs/TEST_REPORT.md](docs/TEST_REPORT.md)) |
| Dashboard | Built into the firmware, checked with the mock server |
| Real hardware | Parts received and wired (9 Nov 2026); firmware compiles for the real board (esp32 core 3.3.12), not yet flashed |
| Enclosure | Designed (Designs Act representation PDFs), not yet printed |

## Quick start

```bash
./tests/run_tests.sh                 # host tests and measurements (needs clang++)
python3 tools/dashboard_mock.py      # dashboard at http://127.0.0.1:8180/
python3 tools/build_wokwi.py         # single-file sketch for wokwi.com
```

Flashing real hardware (Windows, Linux or Mac): [docs/FLASHING.md](docs/FLASHING.md). A ready-to-flash image is in [`release/VibeGuard-esp32-merged.bin`](release/VibeGuard-esp32-merged.bin); write it at address `0x0` from the browser, with nothing to install.

## Repository map

| Path | What it is |
|---|---|
| `firmware/VibeGuard/VibeGuard.ino` | ESP32 sketch: 500 Hz sampler task, DSP task, web server, LED, button, serial commands |
| `firmware/VibeGuard/vg_config.h` | **Every tunable number** (sample rate, bands, thresholds, costs) |
| `firmware/VibeGuard/vg_dsp.h` | Hann window and 256-point FFT |
| `firmware/VibeGuard/vg_diagnosis.h` | Baseline learning, 1× tracking, band energies, decision, health %, advice, JSON |
| `firmware/VibeGuard/vg_sim.h` | Synthetic motor signals, used by Wokwi and the tests |
| `firmware/VibeGuard/vg_dashboard.h` | The dashboard web page (self-contained HTML/JS) |
| `tests/` | Host test harness and generated results |
| `tools/` | Wokwi sketch builder, dashboard mock server |
| `wokwi/` | `diagram.json` (circuit), `wokwi.toml` (VS Code), generated `sketch.ino` |
| `release/` | Prebuilt firmware image (`VibeGuard-esp32-merged.bin`, flash at 0x0) |
| `docs/` | Brief, hardware, algorithm, simulation guide, test report |

## Documentation

- [docs/BRIEF.md](docs/BRIEF.md): project brief and history (the original Claude chat, design PDFs)
- [docs/HARDWARE.md](docs/HARDWARE.md): components, specs, pin map, register setup, BOM, mounting
- [docs/ALGORITHM.md](docs/ALGORITHM.md): signal chain, baseline learning, decision rules, answers to likely examiner questions
- [docs/SIMULATION.md](docs/SIMULATION.md): how to run the host tests, Wokwi and the dashboard mock
- [docs/TEST_REPORT.md](docs/TEST_REPORT.md): measured results from the host tests and the Wokwi run
- [docs/FLASHING.md](docs/FLASHING.md): flash the ESP32 from Windows, Linux or Mac
- [docs/DEMO.md](docs/DEMO.md): how to show each fault on a new motor, and the full shopping list
