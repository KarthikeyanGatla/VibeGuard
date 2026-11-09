// Synthetic motor vibration for the Wokwi simulation and the host tests.
// Wokwi's MPU6050 only holds a static value set by sliders, so in a VG_SIM build this signal is
// added on top of the real sensor reading. All amplitudes are in g (Z axis, gravity excluded).
#pragma once
#include <math.h>
#include <stdint.h>
#include "vg_config.h"

namespace vg {

enum Scenario : uint8_t { SC_HEALTHY, SC_IMBALANCE, SC_LOOSENESS, SC_BEARING, SC_MOTOR_OFF, SC_COUNT };

inline const char* scenarioName(Scenario s) {
  static const char* names[] = {"healthy", "imbalance", "looseness", "bearing", "motor_off"};
  return names[s];
}

// Healthy signature of the reference motor (about the size of a small DC/fan motor at 1500 RPM).
struct MotorProfile {
  float shaftHz = 25.0f;
  float a1 = 0.040f;          // 1x amplitude
  float a2 = 0.008f;          // 2x
  float a3 = 0.004f;          // 3x
  float noise = 0.004f;       // white noise sigma, close to the MPU6050's ~4 mg rms at 260 Hz bandwidth
  float bearingBg = 0.002f;   // background structural resonance
};

class SimSource {
 public:
  explicit SimSource(uint32_t seed = 12345) : rng_(seed ? seed : 1) {
    for (int i = 0; i < kRes; i++) resPh_[i] = 2.0f * kPi * uniform();
  }

  void set(Scenario sc, float severity) { scenario = sc; this->severity = severity; }

  // Next sample in g. severity 0 = healthy amplitudes, 1 = a clear, typical fault.
  float next() {
    float s = severity, f = profile.shaftHz;
    // Phase accumulators (wrapped at 2*pi) keep long runs exact; w is the 1x phase.
    float w = advance(shaftPh_, f);
    float halfW = advance(halfPh_, 0.5f * f);
    float bpfoPh = advance(bpfoPh_, 3.05f * f);
    for (int i = 0; i < kRes; i++) advance(resPh_[i], 150.0f + 15.0f * i);
    float v = profile.noise * gauss();
    if (scenario == SC_MOTOR_OFF) return v;

    float a1 = profile.a1, a2 = profile.a2, a3 = profile.a3, res = profile.bearingBg;
    float sub = 0, high = 0;
    if (scenario == SC_IMBALANCE) a1 *= 1.0f + 2.0f * s;
    if (scenario == SC_LOOSENESS) {
      a1 *= 1.0f + 0.2f * s;
      a2 *= 1.0f + 4.0f * s;
      a3 *= 1.0f + 6.0f * s;
      sub = 0.006f * s;       // 0.5x sub-harmonic
      high = 0.003f * s;      // 4x..6x harmonics
    }
    if (scenario == SC_BEARING) res *= 1.0f + 8.0f * s;

    v += a1 * sinf(w) + a2 * sinf(2 * w + 0.7f) + a3 * sinf(3 * w + 1.9f);
    if (sub > 0) v += sub * sinf(halfW + 0.3f);
    if (high > 0) v += high * (sinf(4 * w + 0.4f) + sinf(5 * w + 2.2f) + sinf(6 * w + 1.1f));

    // Bearing defect: a 150-210 Hz resonance, amplitude-modulated at the outer-race pass frequency.
    float m = 0.5f * (1.0f + cosf(bpfoPh));
    float r = 0;
    for (int i = 0; i < kRes; i++) r += sinf(resPh_[i]);
    v += res * m * r / sqrtf((float)kRes);
    return v;
  }

  MotorProfile profile;
  Scenario scenario = SC_HEALTHY;
  float severity = 1.0f;

 private:
  static constexpr int kRes = 5;
  // Returns the phase for this sample, then steps it by one sample period.
  static float advance(float& ph, float hz) {
    float now = ph;
    ph += 2.0f * kPi * hz / FS_HZ;
    if (ph >= 2.0f * kPi) ph -= 2.0f * kPi;
    return now;
  }
  float uniform() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ >> 8) * (1.0f / 16777216.0f);
  }
  float gauss() {
    float u1 = uniform(), u2 = uniform();
    if (u1 < 1e-7f) u1 = 1e-7f;
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * kPi * u2);
  }
  uint32_t rng_;
  float shaftPh_ = 0, halfPh_ = 0, bpfoPh_ = 0, resPh_[kRes];
};

}  // namespace vg
