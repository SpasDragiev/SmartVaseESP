# SmartVase (ESP32 web dashboard version)

A plant monitoring system on an ESP32. It reads five sensors, shows friendly messages on a 16x2 LCD, and hosts a live web dashboard on your local network with real-time values, history charts, care tips, and a dark/light theme.

## Features

- Reads temperature, humidity, CO₂, soil moisture and soil pH every 5 seconds
- LCD cycles through 5 screens with a short message about the plant's condition
- Local web dashboard served directly from the ESP32, with no cloud and no account
- Live cards with a status and a plant care tip for each sensor
- History charts (about 1 hour, one point every 30 seconds) built with Chart.js
- Dark and light themes, remembered between visits
- Responsive layout for desktop, tablet and phone
- JSON endpoint at `/api` for use in other projects

## Hardware

- ACEBOTT ESP32 board (ESP32-D0WD-V3, Uno form factor)
- SCD40 CO₂ / temperature / humidity sensor
- DHT11 temperature and humidity sensor (shown as extra info on the dashboard)
- Capacitive soil moisture sensor
- DFRobot analog pH sensor
- 16x2 LCD with I2C backpack (address `0x27`)
- Jumper wires and breadboard

## Wiring

> [!NOTE]
> The GPIO numbers below match the `#define` lines at the top of the sketch (`DHTPIN`, `MOISTURE_POWER`, `MOISTURE_PIN`, `PH_PIN`). If your wiring is different, change both the sketch and this table.

| Component | Component pin | ESP32 pin |
|---|---|---|
| DHT11 | Data | **GPIO 25** |
| DHT11 | VCC / GND | 3.3V / GND |
| Soil moisture sensor | Signal (AOUT) | **GPIO 36** (ADC1) |
| Soil moisture sensor | VCC | **GPIO 26** (powered from a pin, so it is only on while measuring) |
| Soil moisture sensor | GND | GND |
| pH sensor | Signal (PO) | **GPIO 39** (ADC1) |
| pH sensor | VCC / GND | 5V / GND |
| SCD40 | SDA | GPIO 21 (SDA) |
| SCD40 | SCL | GPIO 22 (SCL) |
| SCD40 | VCC / GND | 3.3V / GND |
| LCD (I2C) | SDA | GPIO 21 (SDA) |
| LCD (I2C) | SCL | GPIO 22 (SCL) |
| LCD (I2C) | VCC / GND | 5V / GND |

Notes:

- The LCD (`0x27`) and the SCD40 (`0x62`) share the same two I2C wires without conflict.
- Keep the analog sensors on **ADC1 pins** (GPIO 32-39). ADC2 pins stop working while WiFi is on.
- ESP32 analog inputs are limited to 3.3V. Make sure the pH board's output never goes above 3.3V.

## Required libraries

Install these from **Sketch → Include Library → Manage Libraries** in the Arduino IDE. Search for the exact names below.

| Library | Author | Notes |
|---|---|---|
| `Sensirion I2C SCD4x` | Sensirion | SCD40 driver, v1.1.0 or newer. Provides `SensirionI2cScd4x.h`. Also installs `Sensirion Core`. |
| `DHT sensor library` | Adafruit | DHT11 driver |
| `Adafruit Unified Sensor` | Adafruit | Required by the DHT library (the IDE will offer to install it) |
| `LiquidCrystal I2C` | Frank de Brabander | LCD driver |
| `esp32` (board package) | Espressif Systems | Install from **Tools → Board → Boards Manager**. Provides `WiFi.h` and `WebServer.h`. |
| `Wire` | Built in | I2C |

Also used in the browser (loaded from a CDN, nothing to install):

- Chart.js 4.4.1

Includes used in the sketch:

```cpp
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SensirionI2cScd4x.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
```

## Setup

1. Wire the components as in the table above.
2. Install the ESP32 board package and the libraries.
3. Open the `.ino` file and set your WiFi details:

```cpp
   const char* WIFI_SSID = "your-network";
   const char* WIFI_PASS = "your-password";
```

4. In the Arduino IDE choose **ESP32 Dev Module**, then upload. If the upload fails to connect, hold the RST button during upload. Some boards need Flash Mode `DIO` and 40 MHz.
5. After boot the LCD shows the board's IP address (it also prints in the Serial Monitor at 115200 baud).
6. Open that IP in a browser on the same network. The dashboard loads Chart.js from a CDN, so the device viewing it needs internet access.

> [!WARNING]
> Never commit your real WiFi name and password. Keep the placeholders in the repository, or move the credentials into a separate `secrets.h` file and add it to `.gitignore`.

## Calibration

The ESP32 ADC is 12-bit (0-4095), so the values differ from a classic Arduino. The Serial Monitor and the dashboard both show the raw soil value and pH voltage to help.

```cpp
#define DRY_VAL 2817               // sensor in dry air
#define WET_VAL 1700               // sensor in water
#define PH_NEUTRAL_VOLTAGE 2.50    // pH 7.0 buffer
#define PH_ACID_VOLTAGE    2.03    // pH 4.0 buffer
```

For the SCD40, the sketch uses periodic measurement mode. For best CO₂ accuracy, take the sensor outside for a few minutes and run a forced recalibration to 400 ppm, or leave automatic self-calibration on.

## API

`GET /api` returns the current values and history as JSON:

```json
{
  "temp": 22.4, "rh": 51.0, "co2": 640,
  "soil": 58, "soilRaw": 2100, "ph": 6.82, "phV": 2.481,
  "dhtT": 21.9, "dhtH": 54,
  "step": 30,
  "hist": { "temp": [], "rh": [], "co2": [], "soil": [], "ph": [] }
}
```

## Limitations

- History is kept in RAM, so it resets when the board restarts.
- The dashboard only works on the local network.
- The DHT11 is less accurate than the SCD40, so it is shown only as extra info.

## Related

The original Arduino version with SD card logging is available in a separate repository.

## License

MIT
