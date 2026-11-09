# Flashing VibeGuard onto the ESP32 (Windows, Linux or Mac)

Two routes are available. **Route A** needs no software install and takes about 2 minutes. **Route B** builds the program from source.

Board: ESP32 DevKit V1, 30 pins (ESP-WROOM-32). Plug it in with a **data** micro-USB cable. Charge-only cables don't work.

## Route A: browser flasher (easiest, no install)

1. Download [`release/VibeGuard-esp32-merged.bin`](../release/VibeGuard-esp32-merged.bin) from this repo. On GitHub, open the file and click the download icon (⤓) on the right.
   SHA-256: `8d0daedb9acbeae278a8a4501e2ab3bab358a3b6620ff5565dfc61e927b106f8`
2. Open **Google Chrome or Microsoft Edge**. Firefox and Safari can't do this. Go to Espressif's official web flasher: <https://espressif.github.io/esptool-js/>
3. Plug in the ESP32. Set **Baudrate** to `460800` and click **Connect**. Pick the port that appears, which will be named something like `CP2102`, `USB Serial` or `COMx`.
   - If it hangs on "Connecting…", **hold the BOOT button** on the ESP32 until it connects.
4. In the "Flash Address" row, enter **`0x0`** and choose the downloaded `.bin` file.
5. Click **Program** and wait for 100%, which takes about 30–60 s.
6. Press the board's **EN** (reset) button. To see its messages, click **Console** at 115200 baud. It should print `WiFi AP "VibeGuard" ...`.

## Route B: Arduino IDE (build from source)

1. Install **Arduino IDE 2** from <https://www.arduino.cc/en/software>.
2. Go to **File → Preferences → Additional boards manager URLs** and paste:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Go to **Tools → Board → Boards Manager**, search **esp32** and install **"esp32 by Espressif Systems"**. Version 3.3.12 was tested.
4. Download this repo: on GitHub, use **Code → Download ZIP** and unzip it. Then **File → Open** `firmware/VibeGuard/VibeGuard.ino`. The other `vg_*.h` files open as tabs automatically.
5. Under **Tools → Board**, choose **ESP32 Dev Module**. Under **Tools → Port**, pick the ESP32's port.
6. Click **Upload** (→). If it hangs on "Connecting…", hold **BOOT**.
7. Open **Tools → Serial Monitor** at **115200** baud and press **EN**.

No extra libraries are needed.

## USB drivers (only if no port shows up)

| OS | What to do |
|---|---|
| Windows 10/11 | Usually automatic. If Device Manager shows an unknown device, install the **CP210x** driver from Silicon Labs (silabs.com → "CP210x USB to UART Bridge VCP Drivers"). If the board's USB chip is marked CH340, use the WCH CH340 driver instead. |
| Linux | No driver needed. If you get "permission denied" on `/dev/ttyUSB0`, run `sudo usermod -aG dialout $USER`, then log out and back in. |
| macOS | Built in, no driver needed. |

## After flashing

1. On a phone or laptop, join WiFi **VibeGuard** (password `vibeguard`).
2. Open **http://192.168.4.1/** to see the dashboard.
3. To test the button, hold it for 1.5 s. The LED should start fast-blinking (re-learning).

Wiring diagram: [images/wiring_real_board.png](images/wiring_real_board.png). Full details are in [HARDWARE.md](HARDWARE.md).
