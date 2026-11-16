# STM32-Based Indoor Environmental Monitoring and Early-Warning System

A low-cost 32-bit embedded monitor that measures temperature, pressure and a relative gas response, classifies the room as **NORMAL**, **WARNING** or **CRITICAL**, and shows the result locally (OLED, LEDs, buzzer) and on a **live Wi-Fi dashboard** served by an ESP32.

Course project for **21ECC301P – Microprocessor, Microcontroller and Interfacing Techniques**, SRM Institute of Science and Technology.

**Team:** Molya Makam · Bhargavi D · Changalasetty Naimisha

> **Not a safety device.** This is an educational prototype. The gas reading is a relative indicator (not ppm or CO₂), and the thresholds are engineering values, not health limits.

---

## Features

- **Local-first alarms:** the STM32 takes every decision, so the OLED, LEDs and buzzer work with no Wi-Fi.
- **Three states:** NORMAL (green), WARNING (yellow), CRITICAL (red + beeping buzzer).
- **Sensors:** BMP280 (temperature, pressure) and MQ-135 (relative gas response, 12-bit ADC, 8-sample average).
- **Live dashboard:** the ESP32 hosts a web page (status banner, values, 2-minute charts) on the local network.
- **Lean firmware:** no floating point and no heavy libraries, so it fits a 32 KB-flash Blue Pill.
- **Built on two breadboards:** the MQ-135 heater is kept away from the BMP280.

## How it works

```
 BMP280 ──I²C──┐
               ├──► STM32F103 (Blue Pill) ──► OLED, 3 LEDs, buzzer
 MQ-135 ──ADC──┘          │
   (via divider)          └── UART (PA9 → GPIO16) ──► ESP32 ──Wi-Fi──► phone / laptop
```

Every second the STM32 reads the sensors, averages eight ADC samples, applies the rules below, updates the outputs and sends one text line to the ESP32:

```
T=27.4,P=1008,AQ=1520,S=NORMAL
```

| Field | Meaning |
|---|---|
| `T` | Temperature, °C |
| `P` | Pressure, hPa |
| `AQ` | MQ-135 reading, raw 12-bit ADC counts (0–4095) |
| `S` | `NORMAL`, `WARNING` or `CRITICAL` |

### Rules (edit the constants at the top of the sketch)

| Parameter | WARNING | CRITICAL |
|---|---|---|
| Temperature | ≥ 30 °C | ≥ 35 °C |
| Gas reading (ADC counts) | ≥ 1500 | ≥ 2800 |
| Pressure | < 980 or > 1050 hPa | — |

CRITICAL has priority over WARNING. The gas channel is ignored for the first 60 s while the MQ-135 heater warms up. **Tune the gas thresholds to your own clean-air baseline** (a real sensor may read only a few hundred counts in clean air).

## Hardware

| Part | Notes |
|---|---|
| STM32F103 "Blue Pill" | The author's board was reported by STM32CubeProgrammer as a 32 KB low-density part |
| BMP280 | I²C, 3.3 V (address 0x76 or 0x77) |
| MQ-135 module | 5 V heater; analog output through a divider |
| SSD1306 0.96" OLED | I²C, 3.3 V (address 0x3C) |
| ESP32 DevKit | Wi-Fi dashboard (2.4 GHz only) |
| 3 LEDs + 3 × 330 Ω | Green, yellow, red |
| Active buzzer TMB12A05 + 2N2222 + 1 kΩ + 1N4148 | Transistor-driven buzzer with flyback diode |
| 2 × 4.7 kΩ | I²C pull-ups |
| 10 kΩ + 20 kΩ | MQ-135 divider (20 kΩ can be two 10 kΩ in series) |
| ST-Link V2 | Programming over SWD |
| 2 breadboards, jumper wires, 2 micro-USB cables | |

### Pin map

| Signal | STM32 pin | Connects to |
|---|---|---|
| I²C clock / data | PB6 / PB7 | BMP280 + OLED (shared bus, 4.7 kΩ pull-ups to 3.3 V) |
| Gas sensor input | PA0 | MQ-135 AOUT → 10 kΩ → node → 20 kΩ → GND (node goes to PA0) |
| Green / Yellow / Red LED | PA1 / PA2 / PA3 | LED through 330 Ω to GND |
| Buzzer | PA4 | 1 kΩ → 2N2222 base; buzzer + to 5 V |
| UART TX (to ESP32) | PA9 | ESP32 GPIO16 (RX2) |
| UART RX (optional) | PA10 | ESP32 GPIO17 (TX2) |
| Programming | PA13 / PA14 | ST-Link SWDIO / SWCLK |

> **Divider check:** a 5 V module output gives 3.33 V at PA0 with 10 kΩ / 20 kΩ. For more margin, use about 15 kΩ for the lower resistor. Always measure the node with a multimeter before connecting it to PA0.

### Two-breadboard layout

- **Breadboard A (main):** Blue Pill, OLED, BMP280, pull-ups, LEDs, buzzer and driver. Powered from the Blue Pill USB port; rails are +5 V, +3.3 V and GND from the Blue Pill pins.
- **Breadboard B:** ESP32 and MQ-135 with its divider. The ESP32 is powered from its **own USB cable**.
- **Links:** common GND, 5 V to the MQ-135, divider output to PA0, and PA9 to GPIO16.

Wire with power disconnected, check the rails with a multimeter (no 5 V–3.3 V–GND shorts), and power up in stages.

## Software setup

### 1. STM32 firmware (Arduino IDE)

1. Install the **STM32 MCU based boards** package (STMicroelectronics) and **STM32CubeProgrammer**.
2. Install the library **U8g2** (by oliver). The lean sketch needs nothing else.
3. Tools menu: *Board:* Generic STM32F1 series · *Board part number:* BluePill F103C8 (or C6 if listed) · *Upload method:* STM32CubeProgrammer (SWD) · *USB support:* None · *U(S)ART support:* Enabled (generic 'Serial') · *C Runtime Library:* Newlib Nano · *Optimize:* Smallest (-Os).
4. Connect the ST-Link (SWDIO, SWCLK, GND only; BOOT0 and BOOT1 at 0) and upload `live-dashboard/stm32_env_monitor_lean.ino`.
5. Check "Sketch uses … bytes" is **below 32,768** if your chip has 32 KB.

The OLED then shows the title, temperature, pressure, air-quality reading and state; the LEDs and buzzer self-test at start-up.

### 2. ESP32 dashboard

1. Install the **esp32 by Espressif Systems** package; select *ESP32 Dev Module*.
2. Open `live-dashboard/esp32_dashboard.ino` and set `WIFI_SSID` and `WIFI_PASS` (2.4 GHz network).
3. Upload over the ESP32's own USB cable (hold **BOOT** if it stalls on "Connecting…").
4. Open the serial monitor at 115200 baud and read the printed IP address.
5. On a phone or laptop on the **same network**, open `http://<ESP32-IP>/` (or `http://envmonitor.local/`).

**Test without the STM32:** type `T=25.0,P=1013,AQ=1200,S=NORMAL` into the ESP32 serial monitor and press Enter; the dashboard updates.

**Custom dashboard:** edit `live-dashboard/my_dashboard.html` in a browser (it shows demo data when opened from disk), then paste its contents between `R"rawliteral(` and `)rawliteral";` in the ESP32 sketch.

### Dashboard endpoints

| Route | Returns |
|---|---|
| `/` | The dashboard page |
| `/data` | `{"ok":true,"t":25.0,"p":1013,"aq":1520,"s":"NORMAL"}` (or `{"ok":false}` if no frame for 5 s) |

## Simulation (Wokwi)

`stm32-env-monitor/` contains a Wokwi project that stands in a **BMP180** for the BMP280 and a **potentiometer** for the MQ-135, driven by the Arduino-style sketch in `src/main.cpp`. It checks the decision logic and wiring, not real sensor behavior. In the Wokwi web editor, create an STM32 Blue Pill project and paste in `diagram.json`, the sketch and `libraries.txt`.

## Results (simulation)

A discrete-time model of the firmware logic (1 s cycle, 12-bit ADC, thresholds above) gave:

| # | Input (T, P, g) | State | Outputs |
|---|---|---|---|
| 1 | 25 °C, 1013 hPa, 800 | NORMAL | Green |
| 2 | 25 °C, 1013 hPa, 1600 | WARNING | Yellow |
| 3 | 31 °C, 1013 hPa, 800 | WARNING | Yellow |
| 4 | 25 °C, 970 hPa, 800 | WARNING | Yellow |
| 5 | 25 °C, 1013 hPa, 2900 | CRITICAL | Red + buzzer |
| 6 | 36 °C, 1013 hPa, 800 | CRITICAL | Red + buzzer |

State changes occur at the thresholds, and detection delay is at most one sampling period (1 s, mean 0.5 s). Add your own hardware photos and screenshots here.

## Troubleshooting

| Problem | Likely cause / fix |
|---|---|
| `section '.text' will not fit in region 'FLASH'` | Program larger than the chip's flash. Use `stm32_env_monitor_lean.ino`. |
| `Operation exceeds memory limits` on upload | Your chip has 32 KB flash; use the lean sketch. |
| `U8x8lib.h: No such file` | Install the **U8g2** library. |
| `undefined reference to setup/loop` | The sketch tab is empty or wrong; paste the full file into a new sketch. |
| ST-Link "no target" | Check SWDIO/SWCLK/GND wires, BOOT0 at 0; try "connect under reset". |
| OLED blank | SDA/SCL swapped, or check the I²C address. |
| `BMP280: NOT FOUND` | Wiring/address (0x76 or 0x77), or the module is a different chip. |
| Dashboard says NO DATA | Missing common GND, PA9 not wired to GPIO16, or ESP32 not powered. |
| ESP32 won't join Wi-Fi | Wrong password or a 5 GHz-only network. |
| Gas reading always high or low | Divider missing or wrong; the MQ-135 needs warm-up and a clean-air baseline. |

## Limitations

- No humidity or CO₂ measurement; the MQ-135 is a cross-sensitive relative indicator.
- Fixed thresholds with no hysteresis or trend detection, so a noisy reading near a threshold can flip the state.
- Pressure is only a sanity/weather flag, not an air-quality measure.
- The dashboard works on the local Wi-Fi network only.
- The MQ-135 heater (~0.83 W) makes battery operation impractical.

## Future work

BME280 for humidity and a dedicated CO₂ sensor; hysteresis and trend (pressure/gas) detection; gas calibration against a reference; MQTT/cloud logging; soldered PCB and enclosure; a Blue Pill with more flash.

## Repository layout

```
live-dashboard/
  stm32_env_monitor_lean.ino   STM32 firmware for 32 KB flash (use this)
  stm32_env_monitor.ino        Larger Adafruit-library version (needs >32 KB flash)
  esp32_dashboard.ino          ESP32 Wi-Fi dashboard server
  my_dashboard.html            Editable dashboard template (demo data on disk)
stm32-env-monitor/             Wokwi simulation project
docs/                          Report (.docx) and presentation (.pptx)
```

## References

Datasheets: STM32F103x8/xB (DS5319), Bosch BMP280, Winsen MQ-135, Solomon SSD1306, Espressif ESP32. Literature on IoT indoor air quality monitoring and low-cost gas-sensor calibration is listed in the project report.

## License

Not yet specified. Add a license (for example MIT) before publishing.
