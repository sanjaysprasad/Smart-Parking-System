# Smart Parking System — Pinout & Wiring Guide

This guide details the complete hardware connections between the **ESP32 Dev Module**, sensors, displays, and servo actuators for the Smart Parking System.

---

## 📌 Master Wiring & Pinout Table

| Peripheral Component | Component Pin | ESP32 GPIO / Pin | Operating Voltage | Function / Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Entry IR Sensor** | VCC | `5V` (or `3V3`) | 3.3V – 5.0V | Sensor Power |
| | GND | `GND` | Ground | Common Ground |
| | OUT | `GPIO 26` | 3.3V Logic | Active LOW vehicle detection |
| **Exit IR Sensor** | VCC | `5V` (or `3V3`) | 3.3V – 5.0V | Sensor Power |
| | GND | `GND` | Ground | Common Ground |
| | OUT | `GPIO 25` | 3.3V Logic | Active LOW vehicle detection |
| **Entry Gate Servo** | PWM / Signal (Orange/Yellow) | `GPIO 14` | 3.3V / 5.0V PWM | Gate barrier servo control (50 Hz) |
| | VCC (Red) | `+5V (Ext PSU)` | 5.0V (High current) | **External 5V supply recommended** |
| | GND (Brown/Black) | `GND (Common)` | Ground | Must share common ground with ESP32 |
| **Exit Gate Servo** | PWM / Signal (Orange/Yellow) | `GPIO 13` | 3.3V / 5.0V PWM | Gate barrier servo control (50 Hz) |
| | VCC (Red) | `+5V (Ext PSU)` | 5.0V (High current) | **External 5V supply recommended** |
| | GND (Brown/Black) | `GND (Common)` | Ground | Must share common ground with ESP32 |
| **16x2 I2C LCD** | VCC | `5V` / `VIN` | 5.0V | Backlight & display power |
| | GND | `GND` | Ground | Common Ground |
| | SDA | `GPIO 21` | I2C Data | Default ESP32 I2C SDA pin |
| | SCL | `GPIO 22` | I2C Clock | Default ESP32 I2C SCL pin |

---

## ⚡ Important Power & Stability Guidelines

1. **Common Ground is Mandatory:**
   - Always connect the **GND** of the external power supply to the **GND** of the ESP32. Without a common ground reference, the servo PWM signals will jitter or fail to respond.

2. **External Power for Servos:**
   - SG90 or MG90S servos can draw peak stall currents between **500mA and 1000mA** each when moving simultaneously.
   - Powering both servos directly from the ESP32 onboard 3.3V/5V linear regulator will cause **ESP32 brownout resets**. Power servos using a dedicated external 5V (2A+) source or a buck converter module.

3. **IR Sensor Sensitivity Calibration:**
   - Each IR sensor has a built-in trim potentiometer. Before mounting, turn the potentiometer until the onboard sensor indicator LED turns ON only when an object/vehicle is positioned within your desired gate detection distance (e.g., 3–8 cm).

4. **GPIO 25 & GPIO 26 Special Reset:**
   - In standard ESP32 startup, GPIO 25 and GPIO 26 are connected to internal DAC / RTC peripherals. The firmware explicitly resets these pins via `gpio_reset_pin()` to guarantee clean digital input operation without interference.
