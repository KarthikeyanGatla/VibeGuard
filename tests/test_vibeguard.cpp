// Host test harness: runs the firmware's own DSP + diagnosis code (firmware/VibeGuard/*.h)
// against synthetic motor signals and records measurements for docs/TEST_REPORT.md.
//
//   ./tests/run_tests.sh        builds with clang++, runs, writes tests/results/*
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../firmware/VibeGuard/vg_config.h"
#include "../firmware/VibeGuard/vg_diagnosis.h"
#include "../firmware/VibeGuard/vg_dsp.h"
#include "../firmware/VibeGuard/vg_sim.h"

using namespace vg;

static int g_pass = 0, g_fail = 0;
static FILE* g_md = nullptr;  // results/summary.md

static void check(bool ok, const char* name, const std::string& detail) {
  (ok ? g_pass : g_fail)++;
  fprintf(g_md, "| %s | %s | %s |\n", ok ? "PASS" : "**FAIL**", name, detail.c_str());
  printf("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
}

static std::string fmt(const char* f, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

// One frame from the simulator, quantised like the MPU6050 (+-4 g, 8192 LSB/g) with 1 g gravity.
static void frame(SimSource& sim, float* out) {
  for (int i = 0; i < N_FFT; i++) {
    float g = 1.0f + sim.next();
    out[i] = lroundf(g * LSB_PER_G) / LSB_PER_G;
  }
}

static void calibrate(Engine& eng, SimSource& sim) {
  float buf[N_FFT];
  eng.startCalibration();
  sim.set(SC_HEALTHY, 0);
  while (eng.status().state == ST_CALIBRATING) { frame(sim, buf); eng.processFrame(buf); }
}

// ---------------------------------------------------------------------------------------------
static void testFft() {
  fprintf(g_md, "\n## 1. FFT and window\n\n| Result | Test | Measurement |\n|---|---|---|\n");
  static Fft fft;
  float x[N_FFT], p[N_BINS];

  // Against a direct DFT on random data.
  srand(7);
  for (int i = 0; i < N_FFT; i++) x[i] = (rand() / (float)RAND_MAX) - 0.5f;
  fft.powerSpectrum(x, p);
  double maxRel = 0;
  for (int k = 0; k < N_BINS; k++) {
    double re = 0, im = 0;
    for (int n = 0; n < N_FFT; n++) {
      double w = 0.5 - 0.5 * cos(2 * M_PI * n / N_FFT);
      re += x[n] * w * cos(2 * M_PI * k * n / N_FFT);
      im -= x[n] * w * sin(2 * M_PI * k * n / N_FFT);
    }
    double ref = re * re + im * im;
    if (ref > 1e-3) maxRel = fmax(maxRel, fabs(p[k] - ref) / ref);
  }
  check(maxRel < 1e-3, "FFT matches direct DFT", fmt("max relative power error %.2e", maxRel));

  // Amplitude recovery for an on-bin tone (bin 13 = 25.39 Hz), 50 mg.
  float f = 13 * BIN_HZ;
  for (int i = 0; i < N_FFT; i++) x[i] = 0.050f * sinf(2 * kPi * f * i / FS_HZ);
  fft.powerSpectrum(x, p);
  float amp = 2.0f * sqrtf(p[13]) / Fft::WINDOW_SUM;
  check(fabsf(amp - 0.050f) < 0.0005f, "Tone amplitude on bin centre", fmt("50.00 mg in, %.2f mg out", amp * 1000));

  // Worst case: tone half-way between bins (scalloping loss) and energy captured by +-2 bins.
  f = 13.5f * BIN_HZ;
  for (int i = 0; i < N_FFT; i++) x[i] = 0.050f * sinf(2 * kPi * f * i / FS_HZ);
  fft.powerSpectrum(x, p);
  float peak = 2.0f * sqrtf(fmaxf(p[13], p[14])) / Fft::WINDOW_SUM;
  double total = 0;
  for (int k = 1; k < N_BINS; k++) total += p[k];
  double band = Engine::harmonicEnergy(p, f);
  check(peak > 0.035f, "Half-bin tone (scalloping)",
        fmt("peak %.2f mg = %.2f dB loss; +-2-bin band holds %.1f %% of the energy",
            peak * 1000, 20 * log10(peak / 0.050), 100 * band / total));
  fprintf(g_md, "\nResolution: %.3f Hz per bin, frame %.3f s, Nyquist %.0f Hz, %d usable bins.\n",
          BIN_HZ, FRAME_SECONDS, FS_HZ / 2, N_BINS);
}

// ---------------------------------------------------------------------------------------------
static void testShaftEstimate(FILE* csv) {
  fprintf(g_md, "\n## 2. Shaft (1x) frequency estimate\n\n"
                "Calibrate on a healthy motor at each speed, then compare the learned 1x with the true value.\n\n"
                "| Result | Test | Measurement |\n|---|---|---|\n");
  fprintf(csv, "true_hz,estimated_hz,error_hz,found\n");
  double maxErr = 0, sumErr = 0;
  int n = 0, missed = 0;
  for (float hz = 8.5f; hz <= 79.0f; hz += 2.3f) {
    static Engine eng;
    SimSource sim(100 + n);
    sim.profile.shaftHz = hz;
    calibrate(eng, sim);
    float est = eng.status().baseF1Hz;
    double err = fabs(est - hz);
    fprintf(csv, "%.2f,%.3f,%.3f,%d\n", hz, est, est - hz, eng.status().shaftFound);
    maxErr = fmax(maxErr, err);
    sumErr += err;
    n++;
    if (!eng.status().shaftFound) missed++;
  }
  check(maxErr < 0.5 && missed == 0, "1x estimate 8.5-78 Hz (31 speeds)",
        fmt("mean |error| %.3f Hz, max %.3f Hz (bin = %.2f Hz), %d not found", sumErr / n, maxErr, BIN_HZ, missed));
}

// ---------------------------------------------------------------------------------------------
struct RunResult {
  State state;
  Fault fault;
  float health;
  int latencyFrames;  // first frame where the expected fault was declared, -1 if never
};

static RunResult runScenario(Scenario sc, float sev, uint32_t seed, int frames, float driftHz = 0) {
  static Engine eng;
  SimSource sim(seed);
  calibrate(eng, sim);
  sim.profile.shaftHz += driftHz;
  sim.set(sc, sev);
  float buf[N_FFT];
  RunResult r{ST_HEALTHY, F_NONE, 100, -1};
  Fault want = sc == SC_IMBALANCE ? F_IMBALANCE : sc == SC_LOOSENESS ? F_LOOSENESS : sc == SC_BEARING ? F_BEARING : F_NONE;
  for (int i = 0; i < frames; i++) {
    frame(sim, buf);
    eng.processFrame(buf);
    if (r.latencyFrames < 0 && want != F_NONE && eng.status().fault == want) r.latencyFrames = i + 1;
  }
  r.state = eng.status().state;
  r.fault = eng.status().fault;
  r.health = eng.status().health;
  return r;
}

static void testClassification() {
  fprintf(g_md, "\n## 3. Fault classification (severity 1.0, 20 seeds each, 30 frames = 15.4 s after calibration)\n\n");
  const Scenario scs[] = {SC_HEALTHY, SC_IMBALANCE, SC_LOOSENESS, SC_BEARING, SC_MOTOR_OFF};
  const int seeds = 20;
  int confusion[5][6] = {};  // rows: scenario; cols: NONE/IMB/LOOSE/BEAR/MOTOR_OFF/(unused)
  double latSum[5] = {}, healthSum[5] = {};
  int latN[5] = {}, latMax[5] = {};
  for (int s = 0; s < 5; s++) {
    for (int k = 0; k < seeds; k++) {
      RunResult r = runScenario(scs[s], 1.0f, 1000 + 31 * k + s, 30);
      int col = r.state == ST_MOTOR_OFF ? 4 : (int)r.fault;
      confusion[s][col]++;
      healthSum[s] += r.health;
      if (r.latencyFrames > 0) { latSum[s] += r.latencyFrames; latN[s]++; latMax[s] = std::max(latMax[s], r.latencyFrames); }
    }
  }
  fprintf(g_md, "Confusion matrix (rows = simulated condition, columns = VibeGuard's verdict):\n\n"
                "| Simulated \\ Verdict | NONE (healthy) | IMBALANCE | LOOSENESS | BEARING | MOTOR_OFF | Mean health %% |\n"
                "|---|---|---|---|---|---|---|\n");
  int correct = 0;
  for (int s = 0; s < 5; s++) {
    fprintf(g_md, "| %s | %d | %d | %d | %d | %d | %.1f |\n", scenarioName(scs[s]), confusion[s][0], confusion[s][1],
            confusion[s][2], confusion[s][3], confusion[s][4], healthSum[s] / seeds);
    int expectCol = s == 4 ? 4 : s;  // healthy->NONE(0), imbalance->1, looseness->2, bearing->3, off->4
    correct += confusion[s][expectCol];
  }
  fprintf(g_md, "\nDetection latency (frames until the right fault is declared; 1 frame = 0.512 s):\n\n"
                "| Fault | Mean | Worst |\n|---|---|---|\n");
  for (int s = 1; s <= 3; s++)
    fprintf(g_md, "| %s | %.1f frames (%.1f s) | %d frames (%.1f s) |\n", scenarioName(scs[s]), latSum[s] / std::max(1, latN[s]),
            latSum[s] / std::max(1, latN[s]) * FRAME_SECONDS, latMax[s], latMax[s] * FRAME_SECONDS);
  fprintf(g_md, "\n| Result | Test | Measurement |\n|---|---|---|\n");
  check(correct == 5 * seeds, "All 100 runs classified correctly", fmt("%d / %d correct", correct, 5 * seeds));
}

// ---------------------------------------------------------------------------------------------
static void testFalseAlarms() {
  fprintf(g_md, "\n## 4. False alarms on a healthy motor\n\n| Result | Test | Measurement |\n|---|---|---|\n");
  struct Case { const char* name; float drift; } cases[] = {{"Same speed", 0}, {"Speed +5 %", 1.25f}, {"Speed -5 %", -1.25f}, {"Speed +9 %", 2.25f}};
  for (auto& c : cases) {
    static Engine eng;
    long alarmFrames = 0, total = 0;
    float minHealth = 100;
    for (int seed = 0; seed < 10; seed++) {
      SimSource sim(5000 + seed);
      calibrate(eng, sim);
      sim.profile.shaftHz += c.drift;
      float buf[N_FFT];
      for (int i = 0; i < 400; i++) {
        frame(sim, buf);
        eng.processFrame(buf);
        total++;
        if (eng.status().state != ST_HEALTHY) alarmFrames++;
        minHealth = fminf(minHealth, eng.status().health);
      }
    }
    check(alarmFrames == 0, fmt("Healthy, %s", c.name).c_str(),
          fmt("%ld non-healthy frames in %ld (%.0f min of running); lowest health %.1f %%", alarmFrames, total,
              total * FRAME_SECONDS / 60, minHealth));
  }
}

// ---------------------------------------------------------------------------------------------
static void testSeverity(FILE* csv) {
  fprintf(g_md, "\n## 5. Sensitivity: how small a fault is caught\n\n"
                "Severity 0 = healthy, 1 = the nominal fault. Each point is the majority verdict over 5 seeds after 30 frames.\n\n");
  fprintf(csv, "scenario,severity,health,state,fault,ratio_imb,ratio_loose,ratio_bear\n");
  const Scenario scs[] = {SC_IMBALANCE, SC_LOOSENESS, SC_BEARING};
  fprintf(g_md, "| Fault | First severity -> WARNING | First severity -> ALARM | Health at 0.25 / 0.5 / 1.0 / 1.5 |\n|---|---|---|---|\n");
  for (Scenario sc : scs) {
    float firstWarn = -1, firstAlarm = -1;
    float hAt[4] = {0, 0, 0, 0};
    for (int step = 0; step <= 30; step++) {
      float sev = step * 0.05f;
      int warnVotes = 0, alarmVotes = 0;
      float h = 0;
      static Engine eng;
      for (int seed = 0; seed < 5; seed++) {
        SimSource sim(9000 + seed);
        calibrate(eng, sim);
        sim.set(sc, sev);
        float buf[N_FFT];
        for (int i = 0; i < 30; i++) { frame(sim, buf); eng.processFrame(buf); }
        const Status& s = eng.status();
        bool right = s.fault == (Fault)(sc);  // SC_IMBALANCE=1 -> F_IMBALANCE=1, etc.
        if (right && s.state >= ST_WARNING) warnVotes++;
        if (right && s.state == ST_ALARM) alarmVotes++;
        h += s.health;
        if (seed == 0)
          fprintf(csv, "%s,%.2f,%.1f,%s,%s,%.3f,%.3f,%.3f\n", scenarioName(sc), sev, s.health, stateName(s.state),
                  faultName(s.fault), s.ratio[0], s.ratio[1], s.ratio[2]);
      }
      h /= 5;
      if (firstWarn < 0 && warnVotes >= 3) firstWarn = sev;
      if (firstAlarm < 0 && alarmVotes >= 3) firstAlarm = sev;
      if (step == 5) hAt[0] = h;
      if (step == 10) hAt[1] = h;
      if (step == 20) hAt[2] = h;
      if (step == 30) hAt[3] = h;
    }
    const char* what = sc == SC_IMBALANCE ? "1x amplitude x" : sc == SC_LOOSENESS ? "2x amplitude x" : "resonance amplitude x";
    float k = sc == SC_IMBALANCE ? 2.0f : sc == SC_LOOSENESS ? 4.0f : 8.0f;
    fprintf(g_md, "| %s | %.2f (%s%.2f) | %.2f (%s%.2f) | %.0f / %.0f / %.0f / %.0f %% |\n", scenarioName(sc), firstWarn,
            what, 1 + k * firstWarn, firstAlarm, what, 1 + k * firstAlarm, hAt[0], hAt[1], hAt[2], hAt[3]);
  }
}

// ---------------------------------------------------------------------------------------------
static void testEdgeCases() {
  fprintf(g_md, "\n## 6. Edge cases\n\n| Result | Test | Measurement |\n|---|---|---|\n");
  static Engine eng;
  float buf[N_FFT];

  // Calibration length and motor-off rejection during calibration.
  {
    SimSource sim(77);
    eng.startCalibration();
    sim.set(SC_MOTOR_OFF, 0);
    for (int i = 0; i < 10; i++) { frame(sim, buf); eng.processFrame(buf); }
    sim.set(SC_HEALTHY, 0);
    int frames = 0;
    while (eng.status().state == ST_CALIBRATING && frames < 200) { frame(sim, buf); eng.processFrame(buf); frames++; }
    check(frames == CALIB_FRAMES && eng.status().calibRejected == 10, "Calibration skips motor-off frames",
          fmt("10 stopped frames rejected, then %d running frames = %.2f s to learn", frames, frames * FRAME_SECONDS));
  }
  // Motor stops after calibration, then restarts.
  {
    SimSource sim(78);
    calibrate(eng, sim);
    sim.set(SC_MOTOR_OFF, 0);
    for (int i = 0; i < 4; i++) { frame(sim, buf); eng.processFrame(buf); }
    State off = eng.status().state;
    sim.set(SC_HEALTHY, 0);
    for (int i = 0; i < 10; i++) { frame(sim, buf); eng.processFrame(buf); }
    check(off == ST_MOTOR_OFF && eng.status().state == ST_HEALTHY, "Motor stop and restart",
          fmt("stopped -> %s, restarted -> %s (no false fault)", stateName(off), stateName(eng.status().state)));
  }
  // Recovery: fault removed -> back to healthy.
  {
    SimSource sim(79);
    calibrate(eng, sim);
    sim.set(SC_BEARING, 1.0f);
    for (int i = 0; i < 20; i++) { frame(sim, buf); eng.processFrame(buf); }
    State during = eng.status().state;
    sim.set(SC_HEALTHY, 0);
    int back = 0;
    while (eng.status().state != ST_HEALTHY && back < 50) { frame(sim, buf); eng.processFrame(buf); back++; }
    check(during == ST_ALARM && back < 20, "Recovery after repair",
          fmt("returns to HEALTHY %d frames (%.1f s) after the fault is removed", back, back * FRAME_SECONDS));
  }
  // State flicker: found in the Wokwi run, where nominal bearing wear sat right at the alarm
  // threshold and toggled WARNING <-> ALARM. Count state changes once the fault is first declared.
  {
    int flips = 0, runs = 0;
    for (int seed = 0; seed < 10; seed++) {
      SimSource sim(300 + seed);
      calibrate(eng, sim);
      sim.set(SC_BEARING, 1.0f);
      State prev = ST_HEALTHY;
      bool declared = false;
      for (int i = 0; i < 100; i++) {
        frame(sim, buf);
        eng.processFrame(buf);
        State s = eng.status().state;
        if (declared && s != prev) flips++;
        if (s == ST_ALARM) declared = true;
        prev = s;
      }
      runs++;
    }
    check(flips == 0, "No WARNING/ALARM flicker at the threshold",
          fmt("%d state changes after the first ALARM in %d runs of 100 frames (bearing, severity 1.0)", flips, runs));
  }
  // Aliasing: a 270 Hz tone (above Nyquist) shows up at 500-270 = 230 Hz, inside the bearing band.
  {
    static Fft fft;
    float x[N_FFT], p[N_BINS];
    for (int i = 0; i < N_FFT; i++) x[i] = 0.02f * sinf(2 * kPi * 270.0f * i / FS_HZ);
    fft.powerSpectrum(x, p);
    int best = 1;
    for (int k = 1; k < N_BINS; k++) if (p[k] > p[best]) best = k;
    check(fabsf(best * BIN_HZ - 230.0f) < 2 * BIN_HZ, "Aliasing above 250 Hz (documented limitation)",
          fmt("270 Hz input appears at %.1f Hz; MPU6050 DLPF (260 Hz) must remove it in hardware", best * BIN_HZ));
  }
  // Quantisation: smallest step the sensor can resolve.
  check(true, "MPU6050 resolution at +-4 g", fmt("1 LSB = %.3f mg; healthy 1x line = 40 mg = %d LSB", 1000 / LSB_PER_G, (int)(0.040f * LSB_PER_G)));
}

// ---------------------------------------------------------------------------------------------
static void testTiming() {
  fprintf(g_md, "\n## 7. Processing time (host CPU, informational)\n\n");
  static Engine eng;
  SimSource sim(5);
  calibrate(eng, sim);
  float buf[N_FFT];
  frame(sim, buf);
  const int reps = 20000;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < reps; i++) eng.processFrame(buf);
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / reps;
  fprintf(g_md, "processFrame (mean removal, Hann, FFT, bands, decision): %.1f us per frame on this Mac. "
                "The frame budget is %.0f us, so the ESP32 (about 50-100x slower than a desktop core for float work) "
                "still has a large margin. The real ESP32 figure is reported live as `rt.procUs` in /api/status.\n",
          us, FRAME_SECONDS * 1e6);
  fprintf(g_md, "\nMemory used by the engine object: %zu bytes (most of it is the 39-frame calibration store).\n", sizeof(Engine));
}

// ---------------------------------------------------------------------------------------------
// Timeline for tools/dashboard_mock.py: one JSON object per frame, tagged with the scenario.
static void writeTimeline(const char* path) {
  FILE* f = fopen(path, "w");
  static Engine eng;
  SimSource sim(2024);
  eng.startCalibration();
  struct Seg { Scenario sc; float sev; int frames; } segs[] = {
      {SC_HEALTHY, 0, CALIB_FRAMES + 20}, {SC_IMBALANCE, 1, 30}, {SC_HEALTHY, 0, 15}, {SC_LOOSENESS, 1, 30},
      {SC_HEALTHY, 0, 15}, {SC_BEARING, 1, 30}, {SC_HEALTHY, 0, 15}, {SC_MOTOR_OFF, 0, 10}, {SC_HEALTHY, 0, 15}};
  static char st[2048], sp[2048];
  float buf[N_FFT];
  uint32_t frameNo = 0;
  for (auto& s : segs) {
    sim.set(s.sc, s.sev);
    for (int i = 0; i < s.frames; i++) {
      frame(sim, buf);
      eng.processFrame(buf);
      RuntimeInfo rt;
      rt.uptimeMs = (uint32_t)(++frameNo * FRAME_SECONDS * 1000);
      rt.procMicros = 850;  // placeholder in the replay; real value comes from the ESP32
      rt.sim = true;
      rt.scenario = scenarioName(s.sc);
      rt.severity = s.sev;
      statusJson(eng, rt, st, sizeof st);
      spectrumJson(eng, sp, sizeof sp);
      fprintf(f, "{\"scenario\":\"%s\",\"status\":%s,\"spectrum\":%s}\n", scenarioName(s.sc), st, sp);
    }
  }
  fclose(f);
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : "tests/results";
  g_md = fopen((out + "/summary.md").c_str(), "w");
  if (!g_md) { fprintf(stderr, "cannot write to %s\n", out.c_str()); return 2; }
  fprintf(g_md, "# VibeGuard host test results\n\nGenerated by tests/test_vibeguard.cpp. Do not edit by hand; re-run tests/run_tests.sh.\n");
  FILE* shaft = fopen((out + "/shaft_estimate.csv").c_str(), "w");
  FILE* sev = fopen((out + "/severity_sweep.csv").c_str(), "w");

  testFft();
  testShaftEstimate(shaft);
  testClassification();
  testFalseAlarms();
  testSeverity(sev);
  testEdgeCases();
  testTiming();
  writeTimeline((out + "/timeline.jsonl").c_str());

  fprintf(g_md, "\n---\n**%d passed, %d failed.**\n", g_pass, g_fail);
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  fclose(g_md);
  fclose(shaft);
  fclose(sev);
  return g_fail ? 1 : 0;
}
