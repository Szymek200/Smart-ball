# 🔌 ESP32 Firmware: SmartBall Motion & Impact Sensor

This firmware powers an ESP32-based IoT device designed to track 3D orientation, detect high-G impacts, and stream motion data in real-time. Built on the ESP-IDF framework, it uses hardware interrupts, quaternion math, and a circular buffer to capture precise pre-impact and post-impact telemetry.

## 🚀 Key Features

* **Quaternion Sensor Fusion**: Combines accelerometer and gyroscope data to track the device's 3D orientation in world space.
* **Gravity Compensation**: Uses the current orientation to subtract the 1G gravity vector, isolating dynamic acceleration.
* **Pre-Trigger Event Capture**: Utilizes a circular ring buffer to save 100 samples immediately *before* an impact occurs, and 150 samples *after* the impact.
* **Complementary Filter**: Corrects gyroscope drift using accelerometer data with a slow-updating filter coefficient (`K = 0.02f`) to ensure short impacts do not distort the orientation.
* **Automated State Machine**: Automatically transitions from background monitoring to active event recording when dynamic acceleration exceeds the default start threshold of 1.5G. 
## 🏗 Software Architecture

The firmware is modular and relies on FreeRTOS tasks to separate data acquisition from network transmission.

| Module | Description |
| :--- | :--- |
| **`normalize.c` / `.h`** | Handles vector normalization, quaternion multiplication, and the complementary filter to calculate world-frame acceleration. |
| **`circular_buffer.c` / `.h`** | Manages a continuous `historyBuffer` of 256 samples and an `eventBuffer` of 250 samples for impact recording. |
| **`stats.c` / `.h`** | The core impact logic. Calculates vector magnitudes, applies deadzones (ignoring noise < 0.1G), and populates the event buffer when thresholds are breached. |
| **`main.c`** | Manages WiFi SoftAP, UDP sockets, deep sleep power management, and the `sensor_monitor_task` / `udp_server_task` FreeRTOS loops. |
| **Drivers** | `h3lis331dl_reg.c` (Accelerometer) and `L3G4200D.c` (Gyroscope) handle low-level I2C registers. |


## 📡 Impact Detection Logic

The impact event recording follows a strict sequence:
1. **Background**: The sensor writes continuously to the `historyBuffer` (size 256) overwriting old data.
2. **Trigger**: If dynamic acceleration > `HIT_START_THRESHOLD` (1.5G), the `start_hit_event` function fires.
3. **Pre-load**: The firmware immediately copies the last 100 samples (`PRE_SAMPLES`) from the history buffer into the event buffer.
4. **Recording**: Continues recording until the acceleration drops below `HIT_END_THRESHOLD` (1.1G) and 50 post-impact samples (`MIN_POST_SAMPLES`) have passed, or the buffer fills up.

## 🔧 Build & Dependencies

This project is built using the Espressif IoT Development Framework (ESP-IDF) and CMake.

### CMake Configuration
The project registers its custom logic files (`circular_buffer.c`, `normalize.c`, `stats.c`) and requires several core ESP-IDF components to compile successfully:
* `esp_system`
* `freertos`
* `log`
* `driver`
* `newlib` (for math functions like `sqrtf` and `fabsf`)
* `esp_timer`

### Compilation
1. Ensure your ESP-IDF environment is activated (`get_idf`).
2. Run `idf.py build` to compile the firmware.
3. Run `idf.py -p (PORT) flash monitor` to upload and view the serial logs.