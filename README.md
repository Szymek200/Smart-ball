# ⚽ SmartBall: IoT Sports Training System

SmartBall is a comprehensive hardware and software solution designed to enhance sports training by monitoring ball dynamics in real-time. The system focuses on tracking impact force and spin (angular velocity), providing athletes with immediate feedback on their performance.

This project was developed as part of the "Microprocessor and Embedded Systems" course at the Silesian University of Technology (Politechnika Śląska).

## 📂 Repository Structure

This repository is organized into three specialized modules. For detailed technical instructions and implementation logic, please refer to the documentation within each folder:

* **/mobile-app** – Android application (Kotlin) for real-time UDP data visualization, session history, and remote sensor configuration.
* **/firmware-esp32** – ESP-IDF C code featuring high-G impact detection, quaternion-based sensor fusion (complementary filter), and power management.
* **/hardware-3d** – 3D enclosure designs (PET-G), PCB schematics, and the Bill of Materials (BOM).

## ⚙️ System Architecture

The system utilizes a **Producer-Consumer** architecture to ensure high performance and low latency:

* **The Producer (ESP32):** Equipped with an **H3LIS331DL** (400g accelerometer) and an **L3G4200D** (2000 dps gyroscope). It processes raw motion data, compensates for gravity using quaternions, and detects strikes using a circular buffer to capture pre-impact telemetry.
* **The Consumer (Mobile App):** Connects to the ball via a Wi-Fi Access Point and receives processed statistics and live streams using a high-speed binary UDP protocol.

## 🚀 Quick Start Guide

1.  **Power On:** Ensure the Li-Pol battery is charged and the device is powered.
2.  **Connect Wi-Fi:** On your mobile device, connect to the SmartBall network:
    * **SSID:** `SmartBall_ESP32`
    * **Password:** `12345678`
3.  **Launch Application:** Open the SmartBall app to begin receiving data.
4.  **Analyze:** Use the "History" section to view detailed charts of every kick, including G-force and rotation speed.

## 🔋 Power Management

To maximize the 18-hour battery life, the system includes an automated Power Management Algorithm:
* **Deep Sleep:** The MCU enters deep sleep after a configurable period of inactivity.
* **Motion Wake-up:** The accelerometer remains active as a low-power motion detector, waking the ESP32 via a hardware interrupt (GPIO 32) when the ball is moved.

## 🛠 Tech Stack Summary

* **Microcontroller:** ESP32-WROOM-32 (Dual-core).
* **Firmware:** ESP-IDF (C), FreeRTOS, LwIP (UDP).
* **Mobile App:** Kotlin, Coroutines, MPAndroidChart.
* **Mechanical:** 3D Printed PET-G, Li-Pol 1580mAh, MP2636 Power Manager.

---
*Developed by Szymon Mamok | Silesian University of Technology 2025/2026*