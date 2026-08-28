# ESP32 Stage-Talkback & Switched Mic Controller

**ESP32 Stage-Talkback** is an open-source wireless foot controller / pedal designed for vocalists, sound engineers, and stage performers. It enables wireless control over microphone routing using the **OSC (Open Sound Control) protocol over UDP**.

The primary purpose of this device is to temporarily mute a vocal mic from the Main FOH mix and live stream sends while redirecting or maintaining it in stage monitors/talkback buses during live performances.

---

## 🌟 Key Features

* **Multi-Mixer Support:** Compatible with Behringer X32 / Midas M32, Behringer X AIR / Midas MR, Behringer WING, Yamaha (CL/QL/TF/RIVAGE), Allen & Heath (SQ/Qu/dLive), Waves eMotion LV1, and Soundcraft Ui series.
* **Flexible Mute Logic:** Mutes Main LR (FOH) and selected mix buses (Bus 1–16) simultaneously during talkback activation while keeping monitor lines active.
* **Dual Operation Modes:**
  * **Push-To-Talk (PTT):** Microphone is muted only while holding down the button.
  * **Latch (Toggle):** Toggles talkback state on/off with a single press.
* **Wi-Fi Manager & Captive Portal:** Creates a fallback Access Point (`ESP32-Talkback-Setup` at `5.5.5.5`) if no saved Wi-Fi network is detected.
* **Multilingual Web Interface:** Built-in web server with support for 4 languages (English, German, Ukrainian, Russian) to configure Wi-Fi, mixer IP, channel (1–32), bus mute matrix, LED colors, and brightness.
* **Battery Monitoring:** Integrated ADC voltage measurement for Li-Ion/Li-Po batteries (3.3V–4.2V) with real-time percentage readout in the web UI via AJAX.
* **Zero Network Congestion:** Employs hardware debouncing to send single UDP packets only upon state changes, avoiding unnecessary network traffic.
* **NVS Storage & Factory Reset:** Saves all settings in non-volatile storage. Holding down the Reset button on `GPIO 16` for 3 seconds clears memory and reboots to AP mode.

---

## 🛠 Hardware Requirements & Bill of Materials (BOM)

To build this project, you will need the following components:

### Core Components
* **MCU Board:** ESP32 Development Board (e.g., ESP32 DevKit V1 or ESP32-C3 SuperMini).
* **Talkback Switch:** Momentary push-button switch / guitar footswitch connected to `GPIO 4` (uses internal `INPUT_PULLUP`).
* **Reset Button:** Micro tactile button connected to `GPIO 16` (uses internal `INPUT_PULLUP`).
* **Visual Status Indicator:** 3x WS2812B or SK6812 addressable RGB/RGBW LEDs connected to `GPIO 15`.

### Power & Battery Management
* **Battery:** 3.7V Li-Ion / Li-Po cell (e.g., 18650 or flat pouch cell).
* **Charging Module:** TP4056 Li-Ion charger module (Type-C USB).
* **Boost Converter:** MT3608 or DD0812SA step-up converter (boosts 3.7V battery voltage to 5V for ESP32 and LEDs).
* **Power Switch:** SPST mechanical toggle switch placed on the 5V line after the boost converter.
* **Voltage Divider (Battery Monitor):**
  * 2× **10 kΩ resistors** (Wiring: `BAT+` $\rightarrow$ `10k Ω` $\rightarrow$ `GPIO 34` $\rightarrow$ `10k Ω` $\rightarrow$ `GND`).

---

## 📐 Pinout Diagram

| Component | ESP32 Pin | Logic / Connection |
| :--- | :--- | :--- |
| **Talkback Switch** | `GPIO 4` | Active LOW (`INPUT_PULLUP` to `GND`) |
| **Reset Switch** | `GPIO 16` | Active LOW (`INPUT_PULLUP` to `GND`) |
| **WS2812B Data (DIN)** | `GPIO 15` | Digital Output Data Line |
| **Battery Voltage Sensor**| `GPIO 34` | Analog Input (ADC via 10k + 10k divider) |

---

## 🚦 LED Status Indicators

* **Breathing Green:** Device in Access Point mode (`5.5.5.5`).
* **Breathing Yellow:** Connecting to saved Wi-Fi network.
* **Blue (Solid):** Wi-Fi connected, standby state (Mic Live to FOH).
* **Blinking Magenta:** Wi-Fi connected, but mixer is offline/unreachable.
* **Red (Solid):** Talkback mode active (Mic muted from FOH / main mix).

---

## 🚀 Getting Started

1. **Flash Firmware:** Upload the provided `.ino` sketch to your ESP32 board using Arduino IDE (ensure `ESP32 Dev Module` is selected).
2. **Initial Setup:** Power up the device. Connect to the `ESP32-Talkback-Setup` Wi-Fi AP using your smartphone or PC.
3. **Configure Settings:** Open `http://5.5.5.5` or `http://talkback.local` in your web browser. Select your target mixer model, set the mixer's IP address, specify your vocal channel, select which buses to mute during talkback, and click **Save & Reboot**.
