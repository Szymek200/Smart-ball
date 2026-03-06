# 📱 Mobile Application: Measurement Receiver & Analyzer

An Android-based system application designed for remote monitoring of motion parameters, impact detection, and flight phase analysis for an ESP32-based device. Communication is handled in real-time via the UDP protocol.

## 🚀 Key Features

* **Live Monitoring**: Real-time display of raw data from the accelerometer and gyroscope.
* **Impact Detection**: Automatic recognition of collisions based on predefined G-force thresholds.
* **Flight Analysis**: Recording of rotation and duration of the flight phase (the interval between impacts).
* **Data Visualization**: Rendering of dynamic dual-axis charts (Resultant Force vs. Rotation) using the `MPAndroidChart` library.
* **Remote Configuration**: Wireless modification of ESP32 operating parameters such as wakeup thresholds, impact sensitivity, and idle times.
* **CSV Logging**: Optional saving of received samples to a `.csv` file in the phone's internal storage.

## 🛠 Tech Stack

* **Language**: Kotlin.
* **Asynchrony**: Kotlin Coroutines for managing network threads and UI responsiveness.
* **Networking**: UDP Protocol (`DatagramSocket`).
* **Charts**: `MPAndroidChart`.

## 📡 Communication Protocol

The application implements a custom binary protocol (Little Endian) for communicating with the ESP32:

| Header (HEX/ASCII) | Event Description | Application Action |
| :--- | :--- |
| `SMPL` | Live Sample | Updates UI text fields and analyzes flight data. |
| `0xDEADBEEF` | Event Start | Initializes the impact buffer and triggers the event state machine. |
| `0xEEEEEEEE` | Event End | Processes collected samples and adds the entry to the session history. |

### Configuration Commands (Settings)
The app sends text-based commands to control the hardware logic:
* `CMD:SLEEP_EN:[0/1]` – Enable or disable power-saving mode.
* `CMD:THS:[value]` – Hardware wakeup threshold (accelerometer register value 1-127).
* `CMD:HIT:[value]` – Software impact detection threshold in G units.
* `CMD:IDLE:[seconds]` – Time of inactivity before the device enters sleep mode.

## 🏗 Class Structure

* **`MainActivity`**: The primary controller handling the UDP listener, the receiving state machine, and the live dashboard.
* **`HistoryActivity`**: A browser for recorded events featuring detailed charts for every historical impact or flight.
* **`SettingsActivity`**: An interface for adjusting sensor sensitivity and power management.
* **`Models.kt`**: Definitions for data structures (`SampleData`, `HistoryEntry`) and application states.

## 📋 Requirements & Setup

1.  **Android SDK**: Minimum API 24 (Android 7.0).
2.  **Permissions**: Requires `INTERNET` and `ACCESS_WIFI_STATE` permissions.
3.  **Connection**: The phone must be on the same network as the ESP32 (Default IP: `192.168.4.1`, Port: `5000`).
4.  **Dependencies**: Add the `jitpack.io` repository to your `build.gradle` to fetch the `MPAndroidChart` library.

---
*Documentation generated based on the project's source code analysis.*