// VibeGuard diagnosis engine: baseline learning, 1x tracking, band energies, fault decision,
// health % and repair advice. Pure C++ so the host test harness runs exactly this code.
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "vg_config.h"
#include "vg_dsp.h"

namespace vg {

enum State : uint8_t { ST_CALIBRATING, ST_HEALTHY, ST_WARNING, ST_ALARM, ST_MOTOR_OFF };
enum Fault : uint8_t { F_NONE, F_IMBALANCE, F_LOOSENESS, F_BEARING };
enum Band : uint8_t { B_IMB, B_LOOSE, B_BEAR, B_COUNT };

inline const char* stateName(State s) {
  static const char* names[] = {"CALIBRATING", "HEALTHY", "WARNING", "ALARM", "MOTOR_OFF"};
  return names[s];
}
inline const char* faultName(Fault f) {
  static const char* names[] = {"NONE", "IMBALANCE", "LOOSENESS", "BEARING"};
  return names[f];
}
inline const char* bandName(int b) {
  static const char* names[] = {"1x shaft", "2x-3x harmonics", "100-240 Hz broadband"};
  return names[b];
}

struct Advice {
  const char* action;
  int repairInr;
  int failureInr;
};

inline Advice adviceFor(Fault f) {
  switch (f) {
    case F_IMBALANCE: return {"Rotor imbalance: clean the fan/rotor, check for a bent shaft or loose blade, re-balance.", COST_REBALANCE, COST_MOTOR_REPLACEMENT};
    case F_LOOSENESS: return {"Mechanical looseness: tighten the mounting bolts and the clamp, check the base for cracks.", COST_TIGHTEN, COST_MOTOR_REPLACEMENT};
    case F_BEARING:   return {"Bearing wear: replace the bearing now, before it seizes and burns the winding.", COST_BEARING_FIX, COST_MOTOR_REPLACEMENT};
    default:          return {"No action needed. Keep monitoring.", 0, 0};
  }
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Status {
  State state = ST_CALIBRATING;
  Fault fault = F_NONE;
  float health = 100.0f;           // %
  float f1Hz = 0.0f;               // shaft frequency used for this frame
  float baseF1Hz = 0.0f;           // shaft frequency learned during calibration
  bool shaftFound = false;         // a clear 1x peak was found during calibration
  float rmsG = 0.0f;               // time-domain RMS of the frame (DC removed)
  float energy[B_COUNT] = {0, 0, 0};
  float baseline[B_COUNT] = {0, 0, 0};
  float ratio[B_COUNT] = {1, 1, 1};     // EMA-smoothed energy / baseline
  float warnThr[B_COUNT] = {WARN_RATIO, WARN_RATIO, WARN_RATIO};
  float alarmThr[B_COUNT] = {ALARM_RATIO, ALARM_RATIO, ALARM_RATIO};
  int calibDone = 0;
  int calibTotal = CALIB_FRAMES;
  int calibRejected = 0;           // frames skipped because the motor looked stopped
  uint32_t frames = 0;
};

class Engine {
 public:
  void startCalibration() {
    st_ = Status();
    for (int b = 0; b < B_COUNT; b++) persist_[b] = 0;
  }

  // Feed one frame of N_FFT Z-axis samples in g (gravity included is fine; the mean is removed).
  void processFrame(const float* samplesG) {
    float mean = 0;
    for (int i = 0; i < N_FFT; i++) mean += samplesG[i];
    mean /= N_FFT;
    float ss = 0;
    for (int i = 0; i < N_FFT; i++) {
      x_[i] = samplesG[i] - mean;
      ss += x_[i] * x_[i];
    }
    st_.rmsG = sqrtf(ss / N_FFT);
    st_.frames++;
    fft_.powerSpectrum(x_, power_);
    for (int k = 0; k < N_BINS; k++) ampMg_[k] = 2.0f * sqrtf(power_[k]) / Fft::WINDOW_SUM * 1000.0f;

    bool motorOff = st_.rmsG < MOTOR_OFF_RMS_G;
    if (st_.state == ST_CALIBRATING) {
      if (motorOff) { st_.calibRejected++; return; }
      memcpy(calib_[st_.calibDone++], power_, sizeof(power_));
      if (st_.calibDone >= CALIB_FRAMES) finishCalibration();
      return;
    }
    if (motorOff) {
      st_.state = ST_MOTOR_OFF;
      st_.fault = F_NONE;
      for (int b = 0; b < B_COUNT; b++) persist_[b] = 0;
      return;
    }

    // Re-track 1x near the learned speed so small speed changes do not look like faults.
    float tol = fmaxf(st_.baseF1Hz * SHAFT_TRACK_TOL, 2.0f * BIN_HZ);
    bool found = false;
    float f1 = findPeakHz(power_, st_.baseF1Hz - tol, st_.baseF1Hz + tol, &found);
    st_.f1Hz = found ? f1 : st_.baseF1Hz;

    bandEnergies(power_, st_.f1Hz, st_.energy);
    for (int b = 0; b < B_COUNT; b++) {
      float r = (st_.energy[b] + ENERGY_FLOOR) / (st_.baseline[b] + ENERGY_FLOOR);
      st_.ratio[b] += EMA_ALPHA * (r - st_.ratio[b]);
      // Hysteresis: once declared, a band stays active until it falls clearly below the threshold.
      float onThr = persist_[b] >= PERSIST_FRAMES ? st_.warnThr[b] * CLEAR_HYSTERESIS : st_.warnThr[b];
      persist_[b] = st_.ratio[b] >= onThr ? persist_[b] + 1 : 0;
    }
    decide();
  }

  const Status& status() const { return st_; }
  const float* amplitudeMg() const { return ampMg_; }  // N_BINS values, last frame

  // Peak search between loHz and hiHz with parabolic interpolation on log power.
  static float findPeakHz(const float* p, float loHz, float hiHz, bool* found) {
    int lo = (int)ceilf(loHz / BIN_HZ), hi = (int)floorf(hiHz / BIN_HZ);
    if (lo < 1) lo = 1;
    if (hi > N_BINS - 2) hi = N_BINS - 2;
    if (hi < lo) { *found = false; return 0; }
    int best = lo;
    float sum = 0;
    for (int k = lo; k <= hi; k++) {
      sum += p[k];
      if (p[k] > p[best]) best = k;
    }
    float meanP = sum / (hi - lo + 1);
    *found = p[best] > PEAK_PROMINENCE * meanP;
    float a = logf(p[best - 1] + 1e-20f), b = logf(p[best] + 1e-20f), c = logf(p[best + 1] + 1e-20f);
    float den = a - 2.0f * b + c;
    float delta = den != 0.0f ? clampf(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
    return (best + delta) * BIN_HZ;
  }

  static float harmonicEnergy(const float* p, float fHz) {
    int c = (int)lroundf(fHz / BIN_HZ);
    if (c < 1 || c > N_BINS - 1) return 0.0f;
    float e = 0;
    for (int k = c - HARMONIC_HALF_WIDTH; k <= c + HARMONIC_HALF_WIDTH; k++)
      if (k >= 1 && k < N_BINS) e += p[k];
    return e;
  }

  static void bandEnergies(const float* p, float f1, float* e) {
    e[B_IMB] = harmonicEnergy(p, f1);
    e[B_LOOSE] = harmonicEnergy(p, 2.0f * f1) + harmonicEnergy(p, 3.0f * f1);
    int lo = (int)ceilf(BEARING_LO_HZ / BIN_HZ), hi = (int)floorf(BEARING_HI_HZ / BIN_HZ);
    float sum = 0;
    for (int k = lo; k <= hi && k < N_BINS; k++) {
      bool onHarmonic = false;
      for (int m = 1; m <= BEARING_EXCLUDE_HARMONICS && !onHarmonic; m++)
        onHarmonic = fabsf(k - m * f1 / BIN_HZ) <= BEARING_EXCLUDE_HALF_WIDTH + 0.5f;
      if (!onHarmonic) sum += p[k];
    }
    e[B_BEAR] = sum;
  }

 private:
  void finishCalibration() {
    static float avg[N_BINS];
    for (int k = 0; k < N_BINS; k++) {
      float s = 0;
      for (int f = 0; f < CALIB_FRAMES; f++) s += calib_[f][k];
      avg[k] = s / CALIB_FRAMES;
    }
    st_.baseF1Hz = findPeakHz(avg, SHAFT_MIN_HZ, SHAFT_MAX_HZ, &st_.shaftFound);
    st_.f1Hz = st_.baseF1Hz;

    // Per-band mean and spread over the calibration frames set the thresholds for this motor.
    float sum[B_COUNT] = {0, 0, 0}, sum2[B_COUNT] = {0, 0, 0}, e[B_COUNT];
    for (int f = 0; f < CALIB_FRAMES; f++) {
      bandEnergies(calib_[f], st_.baseF1Hz, e);
      for (int b = 0; b < B_COUNT; b++) { sum[b] += e[b]; sum2[b] += e[b] * e[b]; }
    }
    for (int b = 0; b < B_COUNT; b++) {
      float mean = sum[b] / CALIB_FRAMES;
      float var = fmaxf(sum2[b] / CALIB_FRAMES - mean * mean, 0.0f);
      float cv = sqrtf(var) / (mean + ENERGY_FLOOR);
      st_.baseline[b] = mean;
      st_.warnThr[b] = fmaxf(WARN_RATIO, 1.0f + THRESH_K_SIGMA * cv);
      st_.alarmThr[b] = fmaxf(ALARM_RATIO, st_.warnThr[b] * ALARM_RATIO / WARN_RATIO);
      st_.ratio[b] = 1.0f;
      st_.energy[b] = mean;
    }
    st_.state = ST_HEALTHY;
  }

  void decide() {
    int worst = -1;
    float worstScore = 0, maxRatio = 1.0f;
    for (int b = 0; b < B_COUNT; b++) {
      maxRatio = fmaxf(maxRatio, st_.ratio[b]);
      float score = st_.ratio[b] / st_.warnThr[b];
      if (persist_[b] >= PERSIST_FRAMES && score > worstScore) { worstScore = score; worst = b; }
    }
    st_.health = 100.0f * clampf(1.0f - logf(maxRatio) / logf(HEALTH_ZERO_RATIO), 0.0f, 1.0f);
    if (worst < 0) {
      st_.state = ST_HEALTHY;
      st_.fault = F_NONE;
      return;
    }
    Fault f = (Fault)(worst + 1);
    bool wasAlarm = st_.state == ST_ALARM && st_.fault == f;
    float alarmOn = wasAlarm ? st_.alarmThr[worst] * ALARM_HYSTERESIS : st_.alarmThr[worst];
    st_.fault = f;
    st_.state = st_.ratio[worst] >= alarmOn ? ST_ALARM : ST_WARNING;
  }

  Fft fft_;
  Status st_;
  int persist_[B_COUNT] = {0, 0, 0};
  float x_[N_FFT];
  float power_[N_BINS];
  float ampMg_[N_BINS];
  float calib_[CALIB_FRAMES][N_BINS];
};

// ---- JSON for the dashboard (also used by the host mock server) -----------------------------

struct RuntimeInfo {
  uint32_t uptimeMs = 0;
  uint32_t procMicros = 0;     // time spent in processFrame
  uint32_t maxJitterUs = 0;    // worst deviation from the 2000 us sample period
  uint32_t overruns = 0;       // frames dropped because processing was late
  uint32_t i2cErrors = 0;
  bool sim = false;
  const char* scenario = "";
  float severity = 0;
};

inline int statusJson(const Engine& eng, const RuntimeInfo& rt, char* out, size_t n) {
  const Status& s = eng.status();
  Advice a = adviceFor(s.fault);
  return snprintf(out, n,
      "{\"state\":\"%s\",\"fault\":\"%s\",\"health\":%.1f,\"f1\":%.2f,\"baseF1\":%.2f,\"rpm\":%d,"
      "\"shaftFound\":%s,\"rmsMg\":%.2f,"
      "\"ratio\":[%.3f,%.3f,%.3f],\"warn\":[%.2f,%.2f,%.2f],\"alarm\":[%.2f,%.2f,%.2f],"
      "\"energy\":[%.4g,%.4g,%.4g],\"baseline\":[%.4g,%.4g,%.4g],"
      "\"calib\":{\"done\":%d,\"total\":%d,\"rejected\":%d},\"frames\":%lu,"
      "\"advice\":{\"action\":\"%s\",\"repair\":%d,\"failure\":%d,\"saving\":%d},"
      "\"rt\":{\"uptimeMs\":%lu,\"procUs\":%lu,\"jitterUs\":%lu,\"overruns\":%lu,\"i2cErrors\":%lu},"
      "\"sim\":%s,\"scenario\":\"%s\",\"severity\":%.2f}",
      stateName(s.state), faultName(s.fault), s.health, s.f1Hz, s.baseF1Hz, (int)lroundf(s.f1Hz * 60.0f),
      s.shaftFound ? "true" : "false", s.rmsG * 1000.0f,
      s.ratio[0], s.ratio[1], s.ratio[2], s.warnThr[0], s.warnThr[1], s.warnThr[2],
      s.alarmThr[0], s.alarmThr[1], s.alarmThr[2],
      s.energy[0], s.energy[1], s.energy[2], s.baseline[0], s.baseline[1], s.baseline[2],
      s.calibDone, s.calibTotal, s.calibRejected, (unsigned long)s.frames,
      a.action, a.repairInr, a.failureInr, a.failureInr - a.repairInr,
      (unsigned long)rt.uptimeMs, (unsigned long)rt.procMicros, (unsigned long)rt.maxJitterUs,
      (unsigned long)rt.overruns, (unsigned long)rt.i2cErrors,
      rt.sim ? "true" : "false", rt.scenario, rt.severity);
}

inline int spectrumJson(const Engine& eng, char* out, size_t n) {
  int len = snprintf(out, n, "{\"binHz\":%.4f,\"f1\":%.2f,\"bearing\":[%.0f,%.0f],\"mg\":[",
                     BIN_HZ, eng.status().f1Hz, BEARING_LO_HZ, BEARING_HI_HZ);
  const float* a = eng.amplitudeMg();
  for (int k = 0; k < N_BINS && len < (int)n; k++)
    len += snprintf(out + len, n - len, k ? ",%.2f" : "%.2f", a[k]);
  if (len < (int)n) len += snprintf(out + len, n - len, "]}");
  return len;
}

}  // namespace vg
