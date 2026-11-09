# Hardware: components, wiring and measurements

## Block diagram

```
 Motor body ──(strap + saddle, hot-glued sensor)──> MPU6050 (Z axis, ±4 g, DLPF 260 Hz)
                                                       │ I2C @ 400 kHz (SDA 21, SCL 22)
                                                       ▼
 Push button (GPIO 27) ──> ESP32-WROOM-32 ──> Status LED (GPIO 25, amber, 220 Ω)
                           │  core 1: 500 Hz sampler task
                           │  core 0: FFT + diagnosis task, WiFi
                           ▼
                 WiFi access point "VibeGuard" → http://192.168.4.1/ (phone/laptop browser)
```

## Components

| # | Part | Key specs used by VibeGuard | Role |
|---|---|---|---|
| 1 | **ESP32 DevKit V1** (ESP32-WROOM-32) | Dual-core Xtensa LX6 @ 240 MHz, single-precision FPU, 520 KB SRAM, 4 MB flash, 802.11 b/g/n WiFi | Sampling, FFT, diagnosis, web server, access point |
| 2 | **MPU6050** (GY-521 breakout) | 3-axis accel + gyro, 16-bit ADC; ±4 g range = 8192 LSB/g (0.122 mg per LSB); accel noise density about 400 µg/√Hz (≈ 6 mg rms at 260 Hz bandwidth); digital low-pass filter 260 Hz; I2C address 0x68 (AD0 low); about 3.9 mA | Vibration sensor (only the Z axis is used) |
| 3 | Amber 5 mm LED + 220 Ω resistor | ≈ 6 mA from 3.3 V | Status LED (the only thing visible on the sealed enclosure) |
| 4 | Tactile push button | Pulled up internally | Hold 1.5 s = re-learn baseline; short press cycles scenarios in the simulation build |
| 5 | USB 5 V supply / power bank | ESP32 with WiFi AP: ≈ 100–170 mA average, 240 mA peaks | Power |
| 6 | Enclosure (designed, not built yet) | Rounded puck ≈ 70 × 56 × 30 mm, arched saddle for a ≈ 44 mm motor, two strap lugs, amber LED slot | See the design representation PDFs |
| 7 | Strap / hose clamp + hot glue | Rigid coupling: tape damps high frequencies and ruins the bearing band | Mounting |

### Approximate bill of materials

The project quotes **₹1,140** in total. The line items below are typical Indian retail prices and are **estimates**. Replace them with the team's actual receipts.

| Item | Qty | Est. ₹ |
|---|---|---|
| ESP32 DevKit V1 | 1 | 400 |
| MPU6050 GY-521 module | 1 | 150 |
| LED, 220 Ω resistor, push button | 1 set | 15 |
| Breadboard + jumper wires (prototype) | 1 set | 150 |
| USB cable | 1 | 100 |
| 3D-printed enclosure (≈ 25 g PLA) + strap | 1 | 200 |
| Test motor (small DC/fan motor), if bought | 1 | 125 |
| **Total** | | **≈ 1,140** |

## Pin map

| ESP32 pin | Connects to | Notes |
|---|---|---|
| 3V3 | MPU6050 VCC | The GY-521 also accepts 5 V through its regulator. 3.3 V keeps the I2C levels safe. |
| GND | MPU6050 GND, LED cathode (via 220 Ω), button | |
| GPIO 21 | MPU6050 SDA | The GY-521 already has 4.7 kΩ pull-ups |
| GPIO 22 | MPU6050 SCL | 400 kHz |
| GPIO 25 | LED anode | Through 220 Ω to GND |
| GPIO 27 | Push button → GND | `INPUT_PULLUP` |
| (MPU6050 AD0) | leave unconnected or tie to GND | Address 0x68 |
| (MPU6050 INT) | not used | Timing comes from the ESP32 timer |

The same wiring is in [`wokwi/diagram.json`](../wokwi/diagram.json).

### The actual SmartElex board (received 9 Nov 2026)

The SmartElex MPU-6050 comes with its 6-pin headers **loose** (not soldered) and has two 4-pin STEMMA QT / Qwiic sockets. It also includes a cable with a Qwiic plug on one end and bare wire ends on the other. To avoid soldering, use that cable. Get the bare ends tinned at a shop so they stay put in the breadboard.

| Qwiic wire | Signal | ESP32 |
|---|---|---|
| Black | GND | GND |
| Red | 3.3 V | 3V3 |
| Green | SDA | GPIO 21 |
| Yellow | SCL | GPIO 22 |

The received cable's colours are black, red, green and yellow (green takes the place of Qwiic's usual blue). If the firmware reports no MPU6050, swap green and yellow: swapping the two I2C lines can't damage anything. Wiring picture: [images/wiring_real_board.png](images/wiring_real_board.png), drawn from [`wokwi/real_wiring/diagram.json`](../wokwi/real_wiring/diagram.json) (the 30-pin DevKit V1, wiring view only).

## Building and flashing (macOS, arduino-cli)

The firmware needs no external libraries. It was verified on 9 Nov 2026 with arduino-cli 1.5.1 and esp32 core 3.3.12, and uses 74% of flash and 25% of RAM.

```bash
brew install arduino-cli
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index && arduino-cli core install esp32:esp32
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/VibeGuard
arduino-cli board list                      # find the port, e.g. /dev/cu.usbserial-0001
arduino-cli upload --fqbn esp32:esp32:esp32 -p /dev/cu.usbserial-XXXX firmware/VibeGuard
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

If the upload stalls at "Connecting....", hold the board's **BOOT** button until the upload starts.

## MPU6050 register setup (as written by the firmware)

| Register | Value | Meaning |
|---|---|---|
| `PWR_MGMT_1` 0x6B | 0x01 | Wake up, clock from the gyro X PLL (more stable than the internal oscillator) |
| `CONFIG` 0x1A | 0x00 | DLPF 260 Hz accel bandwidth, so the 100–240 Hz bearing band passes |
| `SMPLRT_DIV` 0x19 | 0x00 | Accel output registers update at 1 kHz; the ESP32 reads them at 500 Hz |
| `ACCEL_CONFIG` 0x1C | 0x08 | ±4 g full scale, 8192 LSB/g |
| `WHO_AM_I` 0x75 | read 0x68 | Checked at boot. A wrong value blinks the LED fast and stops. |

Each sample reads two bytes from `ACCEL_ZOUT_H` (0x3F). At 400 kHz that is ≈ 0.1 ms of I2C traffic in each 2 ms sample period.

## Key measurements and figures

| Quantity | Value | Where it comes from |
|---|---|---|
| Sample rate | 500 Hz (2000 µs period, esp_timer) | `FS_HZ` |
| FFT size / frame length | 256 samples / 0.512 s | `N_FFT` |
| Frequency resolution | 1.953 Hz per bin | 500 / 256 |
| Nyquist limit | 250 Hz | 500 / 2 |
| Baseline learning | 39 frames = 19.97 s | `CALIB_FRAMES` |
| Shaft range searched | 8–80 Hz (480–4800 RPM) | `SHAFT_MIN_HZ`, `SHAFT_MAX_HZ` |
| Sensor resolution | 0.122 mg per LSB | ±4 g / 32768 |
| Sensor noise | ≈ 4–6 mg rms | MPU6050 datasheet, 400 µg/√Hz |
| Motor-off threshold | 10 mg rms | `MOTOR_OFF_RMS_G` |
| Engine RAM | 26.7 KB | host test, `sizeof(Engine)` |
| Dashboard page | ≈ 9 KB, no external files | `vg_dashboard.h` |

## Mounting notes

- Mount the sensor **rigidly**: hot glue, epoxy or a screw. Double-sided tape works as a mechanical low-pass filter and hides bearing faults.
- Line the Z axis (the chip's normal) up radially with the motor shaft. That is the direction imbalance pushes in.
- Don't move the sensor after the baseline is learned. If it moves, hold the button for 1.5 s to re-learn.
