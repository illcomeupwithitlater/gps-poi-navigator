# Custom GPS Point-of-Interest Navigator

## Overview
A standalone, battery-operated navigation device that guides users to the nearest open Point-of-Interest (POI) based on real-time temporal data and an onboard database. Designed entirely in C++ (Arduino) for the ESP32, this project manually handles geospatial mathematics, dynamic timezone calculations, and filesystem parsing.

![compass_front](https://github.com/user-attachments/assets/95d6d60e-47ab-4728-bf34-83e24725fac4)

## Technical Stack
* **Microcontroller:** ESP32 (C++ / Arduino)
* **Hardware & Sensors:** GPS Module (via `HardwareSerial`), QMC5883L Magnetometer, SSD1306 OLED Display, WS2812 Addressable LED Ring
* **Libraries:** `LittleFS`, `TinyGPSPlus`, `FastLED`, `Adafruit_GFX`
* **Design Tools:** AutoCAD (Mechanical Enclosure)

## Features
* **Onboard Database System:** Utilizes the ESP32's internal flash memory (`LittleFS`) to store and retrieve POI data from a `.csv` file containing coordinates and operating hours. Includes a custom-built string parser to handle complex, irregular CSV formats without relying on heavy external database libraries.
* **Temporal & Timezone Engine:** * Parses raw UTC time and date from NMEA strings via `TinyGPSPlus`.
  * Dynamically calculates European Daylight Saving Time (DST) using a custom algorithm based on the last Sunday of March and October.
  * Implements Zeller's congruence to mathematically determine the current day of the week.
  * Evaluates POI availability by cross-referencing current local time with parsed opening hour segments (handles "24/7", day ranges, and calculates "minutes until closed").
* **Geospatial Mathematics:** Calculates the distance to candidate POIs using the Haversine formula and computes the required navigational bearing using Great Circle navigation math.
* **Sensor Integration & UI:** * Reads magnetometer data (`QMC5883L`) and applies hardcoded calibration offsets to determine the device's true heading.
  * Computes the relative heading between the device and the target POI, driving a 16-LED `WS2812` ring to point the user in the correct direction.
  * Updates an I2C OLED display with real-time telemetry, including POI name, distance, local time, GPS HDOP, active satellites, and closing time countdown.

## Hardware Pinout
The system is built around ESP32, with peripherals mapped to the following GPIO pins:

| Component / Function | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| **I2C SDA** | `GPIO 21` | Shared bus for QMC5883L (Magnetometer) and SSD1306 (OLED) |
| **I2C SCL** | `GPIO 22` | Shared bus for QMC5883L (Magnetometer) and SSD1306 (OLED) |
| **GPS TX** | `GPIO 16` | Connected to ESP32 HardwareSerial RX2 |
| **GPS RX** | `GPIO 17` | Connected to ESP32 HardwareSerial TX2 |
| **WS2812 LED Data** | `GPIO 5` | Controls the 16-LED addressable ring |

*Note: Power management includes a DC converter and a battery charging circuit housed within the enclosure.*

## Mechanical Design
<img width="691" height="549" alt="image" src="https://github.com/user-attachments/assets/6d0556b7-d65b-400f-8912-671eb521bf94" />

Top part

<img width="743" height="572" alt="image" src="https://github.com/user-attachments/assets/98b84ccf-1d34-4d9f-82d6-2362f281e556" />

Middle part and antenna box

<img width="785" height="626" alt="image" src="https://github.com/user-attachments/assets/e3f3454a-4bf7-48ac-b476-d0737894e800" />

Bottom part

* **Enclosure:** The core hardware is housed in a custom 3-part 3D-printed resin case designed to protect the electronics while minimizing magnetic interference for the QMC5883L magnetometer.
* **RF Interference Mitigation:** During initial prototyping, EMI (Electromagnetic Interference) from the OLED and addressable LED ring caused severe degradation and loss of the GPS signal. To achieve a stable satellite lock, the enclosure was manually modified post-assembly to physically isolate the GPS patch antenna in an external housing.
