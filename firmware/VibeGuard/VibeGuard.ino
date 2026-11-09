// VibeGuard: motor fault diagnosis on an ESP32 + MPU6050.
//
// Samples Z-axis acceleration at 500 Hz, runs a 256-point Hann-windowed FFT every 0.512 s, compares
// three fault bands with a baseline learned in the first 20 s, and serves a dashboard from its own
// WiFi access point at http://192.168.4.1/ .
//
// Build: Arduino IDE / arduino-cli with the "esp32" core (2.x or 3.x), board "ESP32 Dev Module".
// No external libraries. Set VG_SIM to 1 for the Wokwi simulation (synthetic vibration is added to
// the sensor reading, WiFi joins Wokwi-GUEST instead of starting an AP).
#ifndef VG_SIM
#define VG_SIM 0
#endif

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include "esp_timer.h"

#include "vg_config.h"
#include "vg_dashboard.h"
#include "vg_diagnosis.h"
#include "vg_dsp.h"
#include "vg_sim.h"

// ---- Pins (ESP32 DevKit V1 / DevKitC) ----
constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr int PIN_LED = 25;   // amber status LED through 220 ohm
constexpr int PIN_BTN = 27;   // push button to GND (internal pull-up)

// ---- MPU6050 registers ----
constexpr uint8_t MPU_ADDR = 0x68;
constexpr uint8_t REG_SMPLRT_DIV = 0x19, REG_CONFIG = 0x1A, REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_ZOUT_H = 0x3F, REG_PWR_MGMT_1 = 0x6B, REG_WHO_AM_I = 0x75;

constexpr char AP_SSID[] = "VibeGuard";
constexpr char AP_PASS[] = "vibeguard";   // WPA2 needs at least 8 characters
constexpr uint32_t SAMPLE_PERIOD_US = (uint32_t)(1000000.0f / vg::FS_HZ);  // 2000 us

static vg::Engine engine;
static vg::RuntimeInfo runtime;
static WebServer server(80);
static SemaphoreHandle_t engineLock;
static TaskHandle_t samplerTask, dspTask;

// Double buffer: the sampler fills one half while the DSP task processes the other.
static int16_t frameBuf[2][vg::N_FFT];
static volatile int readyBuf = -1;

#if VG_SIM
static vg::SimSource sim;
#endif

// ---------------------------------------------------------------------------------------------
static bool mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool mpuRead(uint8_t reg, uint8_t* out, uint8_t n) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

static bool mpuBegin() {
  uint8_t who = 0;
  if (!mpuRead(REG_WHO_AM_I, &who, 1) || who != 0x68) {
    Serial.printf("MPU6050 not found (WHO_AM_I=0x%02X). Check SDA=%d SCL=%d.\n", who, PIN_SDA, PIN_SCL);
    return false;
  }
  return mpuWrite(REG_PWR_MGMT_1, 0x01) &&     // wake, PLL on gyro X clock
         mpuWrite(REG_CONFIG, 0x00) &&         // DLPF 260 Hz accel bandwidth (keeps the 100-240 Hz band)
         mpuWrite(REG_SMPLRT_DIV, 0x00) &&     // accel registers update at 1 kHz; we read at 500 Hz
         mpuWrite(REG_ACCEL_CONFIG, 0x08);     // +-4 g -> 8192 LSB/g
}

// ---------------------------------------------------------------------------------------------
// Sampling: esp_timer ticks every 2 ms and wakes a high-priority task that reads the sensor.
static void IRAM_ATTR onSampleTimer(void*) { xTaskNotifyGive(samplerTask); }

static void samplerLoop(void*) {
  int buf = 0, pos = 0;
  int64_t lastUs = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (lastUs) {
      int64_t dev = (now - lastUs) - (int64_t)SAMPLE_PERIOD_US;
      uint32_t adev = (uint32_t)(dev < 0 ? -dev : dev);
      if (adev > runtime.maxJitterUs) runtime.maxJitterUs = adev;
    }
    lastUs = now;

    uint8_t b[2];
    int16_t z = 0;
    if (mpuRead(REG_ACCEL_ZOUT_H, b, 2)) z = (int16_t)((b[0] << 8) | b[1]);
    else runtime.i2cErrors++;
#if VG_SIM
    int32_t v = z + (int32_t)lroundf(sim.next() * vg::LSB_PER_G);
    z = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
#endif
    frameBuf[buf][pos++] = z;
    if (pos == vg::N_FFT) {
      if (readyBuf != -1) runtime.overruns++;  // DSP did not finish the previous frame in time
      readyBuf = buf;
      buf ^= 1;
      pos = 0;
      xTaskNotifyGive(dspTask);
    }
  }
}

static void dspLoop(void*) {
  static float g[vg::N_FFT];
  uint32_t logCount = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    int idx = readyBuf;
    if (idx < 0) continue;
    for (int i = 0; i < vg::N_FFT; i++) g[i] = frameBuf[idx][i] / vg::LSB_PER_G;
    readyBuf = -1;

    xSemaphoreTake(engineLock, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    engine.processFrame(g);
    runtime.procMicros = (uint32_t)(esp_timer_get_time() - t0);
    const vg::Status s = engine.status();
    xSemaphoreGive(engineLock);

    if (++logCount % 2 == 0) {  // about once a second
      Serial.printf("[%7.1fs] %-11s %-9s health=%5.1f%% f1=%5.2fHz rpm=%4d rms=%5.1fmg R=%.2f/%.2f/%.2f dsp=%luus",
                    millis() / 1000.0f, vg::stateName(s.state), vg::faultName(s.fault), s.health, s.f1Hz,
                    (int)lroundf(s.f1Hz * 60), s.rmsG * 1000, s.ratio[0], s.ratio[1], s.ratio[2],
                    (unsigned long)runtime.procMicros);
      if (s.state == vg::ST_CALIBRATING) Serial.printf(" calib=%d/%d", s.calibDone, s.calibTotal);
#if VG_SIM
      Serial.printf(" sim=%s@%.2f", vg::scenarioName(sim.scenario), sim.severity);
#endif
      Serial.println();
    }
  }
}

// ---------------------------------------------------------------------------------------------
static void recalibrate() {
  xSemaphoreTake(engineLock, portMAX_DELAY);
  engine.startCalibration();
  xSemaphoreGive(engineLock);
  Serial.println("Re-learning baseline: keep the motor running in its healthy state for 20 s.");
}

#if VG_SIM
static void setScenario(vg::Scenario sc, float severity) {
  sim.set(sc, severity);
  Serial.printf("SIM scenario -> %s (severity %.2f)\n", vg::scenarioName(sc), severity);
}
#endif

static void sendJson(int (*fill)(char*, size_t)) {
  static char buf[2600];
  xSemaphoreTake(engineLock, portMAX_DELAY);
  fill(buf, sizeof buf);
  xSemaphoreGive(engineLock);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", buf);
}

static int fillStatus(char* out, size_t n) {
  runtime.uptimeMs = millis();
#if VG_SIM
  runtime.sim = true;
  runtime.scenario = vg::scenarioName(sim.scenario);
  runtime.severity = sim.severity;
#endif
  return vg::statusJson(engine, runtime, out, n);
}
static int fillSpectrum(char* out, size_t n) { return vg::spectrumJson(engine, out, n); }

static void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/status", HTTP_GET, [] { sendJson(fillStatus); });
  server.on("/api/spectrum", HTTP_GET, [] { sendJson(fillSpectrum); });
  server.on("/api/recalibrate", HTTP_POST, [] { recalibrate(); server.send(200, "application/json", "{\"ok\":true}"); });
#if VG_SIM
  server.on("/api/sim", HTTP_POST, [] {
    String name = server.arg("scenario");
    float sev = server.hasArg("severity") ? server.arg("severity").toFloat() : sim.severity;
    for (int i = 0; i < vg::SC_COUNT; i++)
      if (name == vg::scenarioName((vg::Scenario)i)) setScenario((vg::Scenario)i, sev);
    server.send(200, "application/json", "{\"ok\":true}");
  });
#endif
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

static void setupWifi() {
#if VG_SIM
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);  // Wokwi has no soft-AP; its gateway forwards port 80 instead
  Serial.println("WiFi: joining Wokwi-GUEST (simulation build)");
#else
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("WiFi AP \"%s\" (password \"%s\"), dashboard at http://%s/\n", AP_SSID, AP_PASS,
                WiFi.softAPIP().toString().c_str());
#endif
}

// ---------------------------------------------------------------------------------------------
// Status LED: calibrating = 2 Hz blink, healthy = short blip every 3 s, warning = 1 Hz, alarm = 5 Hz,
// motor off = off.
static void updateLed() {
  uint32_t t = millis();
  bool on = false;
  switch (engine.status().state) {
    case vg::ST_CALIBRATING: on = (t / 250) % 2; break;
    case vg::ST_HEALTHY:     on = (t % 3000) < 80; break;
    case vg::ST_WARNING:     on = (t / 500) % 2; break;
    case vg::ST_ALARM:       on = (t / 100) % 2; break;
    default:                 on = false; break;
  }
  digitalWrite(PIN_LED, on);
}

// Button: hold 1.5 s = re-learn baseline. In the simulation build a short press cycles the scenario.
static void updateButton() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  static bool longDone = false;
  bool down = digitalRead(PIN_BTN) == LOW;
  uint32_t t = millis();
  if (down && !wasDown) { downAt = t; longDone = false; }
  if (down && !longDone && t - downAt > 1500) { longDone = true; recalibrate(); }
  if (!down && wasDown && !longDone && t - downAt > 30) {
#if VG_SIM
    setScenario((vg::Scenario)((sim.scenario + 1) % vg::SC_COUNT), sim.severity);
#endif
  }
  wasDown = down;
}

// Serial commands: r = re-learn, j = print status JSON; simulation build adds
// h/i/l/b/o = healthy/imbalance/looseness/bearing/motor off, 0-9 = severity 0.0-1.8.
static void updateSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') recalibrate();
    if (c == 'j') { static char buf[2600]; xSemaphoreTake(engineLock, portMAX_DELAY); fillStatus(buf, sizeof buf); xSemaphoreGive(engineLock); Serial.println(buf); }
#if VG_SIM
    const char* keys = "hilbo";
    for (int i = 0; i < vg::SC_COUNT; i++) if (c == keys[i]) setScenario((vg::Scenario)i, sim.severity);
    if (c >= '0' && c <= '9') setScenario(sim.scenario, (c - '0') * 0.2f);
#endif
  }
}

// ---------------------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nVibeGuard starting");
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  if (!mpuBegin()) {
    for (;;) { digitalWrite(PIN_LED, (millis() / 100) % 2); delay(10); }  // fast blink forever = sensor fault
  }
  Serial.println("MPU6050 ready: +-4 g, DLPF 260 Hz, sampling Z at 500 Hz");

  engineLock = xSemaphoreCreateMutex();
  engine.startCalibration();
#if VG_SIM
  setScenario(vg::SC_HEALTHY, 1.0f);
#endif
  setupWifi();
  setupWeb();

  xTaskCreatePinnedToCore(dspLoop, "vg_dsp", 6144, nullptr, 3, &dspTask, 0);
  xTaskCreatePinnedToCore(samplerLoop, "vg_sample", 4096, nullptr, 10, &samplerTask, 1);
  const esp_timer_create_args_t args = {.callback = onSampleTimer, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
                                        .name = "vg_tick", .skip_unhandled_events = true};
  esp_timer_handle_t timer;
  esp_timer_create(&args, &timer);
  esp_timer_start_periodic(timer, SAMPLE_PERIOD_US);
  Serial.println("Learning the healthy baseline for 20 s...");
}

void loop() {
  server.handleClient();
  updateLed();
  updateButton();
  updateSerial();
  delay(2);
}
