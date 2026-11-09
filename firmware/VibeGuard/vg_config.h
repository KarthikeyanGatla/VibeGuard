// VibeGuard configuration: every tunable number lives here.
// Shared by the ESP32 firmware and the host test harness (no Arduino dependencies).
#pragma once
#include <stdint.h>

namespace vg {

// ---- Sampling and FFT ------------------------------------------------------
constexpr float FS_HZ = 500.0f;                 // MPU6050 Z-axis sample rate
constexpr int   N_FFT = 256;                    // samples per frame (must be a power of two)
constexpr int   N_BINS = N_FFT / 2;             // usable bins 0..127 (Nyquist bin dropped)
constexpr float BIN_HZ = FS_HZ / N_FFT;         // 1.953 Hz per bin
constexpr float FRAME_SECONDS = N_FFT / FS_HZ;  // 0.512 s per frame
constexpr float kPi = 3.14159265358979f;

// ---- MPU6050 ---------------------------------------------------------------
constexpr float LSB_PER_G = 8192.0f;            // +-4 g full scale (ACCEL_CONFIG = 0x08)

// ---- Baseline learning -----------------------------------------------------
constexpr float CALIB_SECONDS = 20.0f;
constexpr int   CALIB_FRAMES = int(CALIB_SECONDS / FRAME_SECONDS + 0.5f);  // 39 frames = 19.97 s

// ---- Shaft (1x) frequency search ------------------------------------------
constexpr float SHAFT_MIN_HZ = 8.0f;            // 480 RPM
constexpr float SHAFT_MAX_HZ = 80.0f;           // 4800 RPM
constexpr float SHAFT_TRACK_TOL = 0.10f;        // re-track 1x within +-10% of the learned value
constexpr float PEAK_PROMINENCE = 4.0f;         // peak must be 4x the mean power of the search range

// ---- Fault bands -----------------------------------------------------------
constexpr int   HARMONIC_HALF_WIDTH = 2;        // bins either side of 1x/2x/3x (Hann main lobe)
constexpr float BEARING_LO_HZ = 100.0f;
constexpr float BEARING_HI_HZ = 240.0f;         // stays below the 250 Hz Nyquist limit
constexpr int   BEARING_EXCLUDE_HARMONICS = 8;  // 1x..8x lines are removed from the bearing band
constexpr int   BEARING_EXCLUDE_HALF_WIDTH = 1;

// ---- Decision logic --------------------------------------------------------
// Ratios are band energy / learned baseline energy (energy ratio 4 = amplitude x2 = +6 dB).
constexpr float WARN_RATIO = 3.0f;              // minimum warning threshold (+4.8 dB)
constexpr float ALARM_RATIO = 6.0f;             // minimum alarm threshold   (+7.8 dB)
constexpr float THRESH_K_SIGMA = 4.0f;          // warn >= 1 + k * (baseline std / mean)
constexpr float HEALTH_ZERO_RATIO = 16.0f;      // ratio at which health reaches 0 % (+12 dB)
constexpr float EMA_ALPHA = 0.3f;               // smoothing of the per-frame ratios
constexpr int   PERSIST_FRAMES = 3;             // frames above threshold before a fault is declared
constexpr float CLEAR_HYSTERESIS = 0.8f;        // a declared fault clears only below 0.8 x warning
constexpr float ALARM_HYSTERESIS = 0.85f;       // an alarm drops to warning only below 0.85 x alarm
constexpr float MOTOR_OFF_RMS_G = 0.010f;       // below this the motor is treated as stopped
constexpr float ENERGY_FLOOR = 1e-9f;           // avoids dividing by an empty band

// ---- Advisory costs (INR) --------------------------------------------------
// Bearing and motor figures come from the project pitch; the others are estimates, edit to suit.
constexpr int COST_MOTOR_REPLACEMENT = 1800;
constexpr int COST_BEARING_FIX = 150;
constexpr int COST_REBALANCE = 100;
constexpr int COST_TIGHTEN = 50;

}  // namespace vg
