# 🔐 Secure Evidence Safety Box

An **IoT-based embedded security system** designed for controlled storage and monitoring of sensitive evidence, documents, and physical assets.

The system combines **electronic access control, tamper detection, environmental monitoring, Bluetooth communication, GPS location logging, and a menu-driven OLED interface** into a single embedded security solution.

> **Project Type:** IoT / Embedded Systems / Security Automation
> **Platform:** Arduino-based embedded system
> **Communication:** HC-05 Bluetooth
> **Interface:** OLED + 4×4 Keypad
> **Storage:** EEPROM

---

## 📌 Project Overview

The Secure Evidence Safety Box is designed to provide multiple layers of protection for physically stored evidence or sensitive materials.

The system keeps the box **locked by default** and allows authorized access through a 4-digit PIN or a generated random PIN. While the box is operating, environmental and motion sensors continuously monitor its condition.

If abnormal movement, excessive environmental conditions, or an unauthorized access attempt is detected, the system activates a buzzer and can transmit alerts through Bluetooth.

Configuration data such as the access PIN and sensor thresholds are stored in EEPROM, allowing the settings to persist even after power loss.

---

## ✨ Key Features

### 🔐 Electronic Access Control

* 4-digit PIN-based electronic locking
* Secure PIN change workflow
* Old PIN verification before changing credentials
* Random 4-digit PIN generation for temporary access
* Masked PIN entry on OLED
* Wrong PIN detection with buzzer feedback
* Automatic re-locking after successful access
* EEPROM-based persistent PIN storage
* Relay-controlled electronic lock

## The firmware controls the relay to unlock the box temporarily and automatically returns it to the locked state after authorized access.

### 🚨 Tamper Detection

The system uses an **MPU6050 accelerometer** to detect abnormal physical movement.

It monitors:

* Significant box tilting
* Sudden X-axis movement
* Sudden Y-axis movement
* Sudden Z-axis movement
* Vibration/shaking conditions

When a tamper condition is detected:

1. The event is identified by the controller.
2. The buzzer is activated.
3. A tamper message is displayed/sent.
4. The event can be transmitted to a connected Bluetooth device.

The current firmware implements tilt and acceleration thresholds using MPU6050 readings.

---

### 🌡️ Environmental Monitoring

A **BME680 environmental sensor** continuously monitors the internal environment of the box.

Measured parameters:

* 🌡️ Temperature
* 💧 Humidity
* 💨 Gas resistance / air-quality-related reading

The system supports configurable safety thresholds for:

* Temperature
* Humidity
* Gas resistance

When a configured threshold is violated, the system activates an alert through the buzzer and Bluetooth communication.

---

### 💾 Persistent Configuration

The system uses **EEPROM non-volatile memory** to preserve important configuration data.

Stored parameters include:

* Access PIN
* Temperature threshold
* Humidity threshold
* Gas threshold

The firmware validates stored values during startup and loads them into the active configuration.

This allows the system to retain configuration even after power loss or restart.

---

### 📱 Bluetooth Communication

An **HC-05 Bluetooth module** provides two-way communication between the embedded system and a mobile device.

The system can transmit:

* Live sensor readings
* Lock status
* Tamper alerts
* Sensor alerts
* PIN-related status messages
* GPS/location information
* System status information

The firmware also periodically transmits live BME680 readings to the connected Bluetooth device.

---

### 📍 GPS / Location Logging

The current implementation supports **location information received through Bluetooth or Serial communication** rather than reading GPS coordinates directly from a GPS hardware module.

Supported input formats include:

```text
GPS:<location>
```

and plain-text location information such as:

```text
BGSCET Malleshwaram
```

The latest received location is stored in memory and displayed through the GPS menu.

This approach allows a mobile device to provide the location information while the embedded system stores and displays the latest known location.

---

### 🖥️ OLED User Interface

The system uses a **128×64 SH1106 OLED display** with the U8G2 graphics library.

The interface provides:

* Main menu
* Device lock status
* Security/PIN management
* GPS mode
* Sensor threshold configuration
* Live sensor values
* PIN entry
* Random PIN generation
* About/project information
* Restart interface

The display is combined with a **4×4 matrix keypad** for local interaction.

---

## 🧩 Hardware Components

| Component                          | Purpose                                             |
| ---------------------------------- | --------------------------------------------------- |
| Arduino-compatible microcontroller | Main embedded controller                            |
| BME680                             | Temperature, humidity and gas-resistance monitoring |
| MPU6050                            | Motion and tamper detection                         |
| SH1106 128×64 OLED                 | User interface and system status                    |
| 4×4 Matrix Keypad                  | Local user input                                    |
| HC-05 Bluetooth                    | Wireless communication                              |
| Relay Module                       | Electronic lock control                             |
| Buzzer                             | Security and sensor alerts                          |
| EEPROM                             | Persistent configuration storage                    |

---

## 🔌 Core Pin Configuration

The current firmware uses the following primary connections:

| Component      | Pin / Interface |
| -------------- | --------------- |
| Relay          | D2              |
| Buzzer         | A15             |
| Keypad Rows    | D10, D9, D8, D7 |
| Keypad Columns | D6, D5, D4, D3  |
| Bluetooth RX   | D14             |
| Bluetooth TX   | D15             |
| OLED           | I²C             |
| BME680         | I²C             |
| MPU6050        | I²C             |

The exact wiring should be verified against the hardware used for your physical prototype before deployment.

---

## 🔄 System Workflow

```text
                ┌──────────────────────┐
                │    System Startup   │
                └──────────┬───────────┘
                           │
                           ▼
                ┌──────────────────────┐
                │ Initialize Hardware  │
                │ OLED / BME680 / MPU  │
                │ Bluetooth / EEPROM   │
                └──────────┬───────────┘
                           │
                           ▼
                ┌──────────────────────┐
                │    Box LOCKED        │
                └──────────┬───────────┘
                           │
             ┌─────────────┼──────────────┐
             ▼             ▼              ▼
        ┌─────────┐   ┌──────────┐   ┌──────────┐
        │  Keypad │   │ Sensors  │   │Bluetooth │
        │ Input   │   │Monitoring│   │Communication│
        └────┬────┘   └────┬─────┘   └────┬─────┘
             │             │              │
             ▼             ▼              ▼
        ┌─────────┐   ┌──────────┐   ┌──────────┐
        │  PIN /  │   │ Tamper / │   │ Status & │
        │ Random  │   │ Sensor   │   │ Location │
        │   PIN   │   │  Alert   │   │  Updates │
        └────┬────┘   └────┬─────┘   └──────────┘
             │             │
             ▼             ▼
        ┌─────────┐   ┌──────────┐
        │ Unlock  │   │  Buzzer  │
        │  Relay  │   │  Alert   │
        └────┬────┘   └──────────┘
             │
             ▼
        ┌─────────────┐
        │ Auto-ReLock │
        └─────────────┘
```

---

## 🔑 Access Control Flow

```text
User
 │
 ▼
Unlock Menu
 │
 ├──► Stored PIN
 │       │
 │       ├── Correct ──► Unlock ──► Auto-ReLock
 │       │
 │       └── Incorrect ──► Buzzer Alert
 │
 └──► Random PIN
         │
         ├── Correct ──► Unlock ──► Auto-ReLock
         │
         └── Incorrect ──► Buzzer Alert
```

---

## 📡 Bluetooth Communication

### Commands Received

| Command             | Function                               |
| ------------------- | -------------------------------------- |
| `GPS:<location>`    | Store location information             |
| `GPS_REQUEST`       | Request location from connected device |
| `REQUEST_PINS`      | Request stored/random PIN information  |
| Plain text location | Store location text                    |

### Messages Transmitted

| Message           | Purpose                       |
| ----------------- | ----------------------------- |
| `GPS STORED:`     | Confirms location storage     |
| `STORED_PIN:`     | Sends stored PIN information  |
| `RANDOM_PIN:`     | Sends generated random PIN    |
| `PIN_CHANGED`     | Confirms PIN update           |
| `SENSOR ALERT`    | Environmental threshold alert |
| `TAMPER DETECTED` | Physical tamper alert         |
| `Live-T:`         | Live temperature              |
| `H:`              | Live humidity                 |
| `G:`              | Live gas-resistance reading   |

---

## 📊 Sensor Monitoring Logic

The BME680 values are continuously read and stored by the firmware.

The system generates an environmental alert when:

```text
Temperature > Temperature Threshold
        OR
Humidity > Humidity Threshold
        OR
Gas Resistance < Gas Threshold
```

The threshold values can be modified through the OLED/keypad interface and are stored in EEPROM.

---

## 🛠️ Software Architecture

The firmware is organized into functional modules:

```text
Secure Evidence Safety Box
│
├── Hardware Initialization
│
├── OLED Display Management
│
├── Keypad Input Handling
│
├── PIN Authentication
│   ├── Stored PIN
│   └── Random PIN
│
├── Relay Lock Control
│
├── Tamper Detection
│   └── MPU6050
│
├── Environmental Monitoring
│   └── BME680
│
├── Bluetooth Communication
│   └── HC-05
│
├── GPS / Location Handling
│
├── EEPROM Configuration
│
└── Alert Management
    └── Buzzer
```

---

## 📚 Required Arduino Libraries

Install the following libraries through the Arduino IDE Library Manager:

```text
U8g2
Keypad
Adafruit BME680 Library
Adafruit Unified Sensor
SoftwareSerial
MPU6050
```

The firmware includes these libraries directly.

---

## 🚀 Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/YOUR-USERNAME/secure-evidence-safety-box.git
cd secure-evidence-safety-box
```

### 2. Open the Firmware

Open:

```text
firmware/Secure_Evidence_Box.ino
```

using the Arduino IDE.

### 3. Install Dependencies

Install the required libraries listed above.

### 4. Connect the Hardware

Connect the OLED, keypad, BME680, MPU6050, HC-05, relay and buzzer according to the project wiring.

### 5. Configure the Board

Select the microcontroller board and appropriate COM/USB port in Arduino IDE.

### 6. Upload the Firmware

Compile and upload the `.ino` file.

### 7. Verify the System

After startup, verify:

* OLED initialization
* BME680 sensor readings
* MPU6050 calibration
* Bluetooth connection
* Keypad input
* Relay lock operation
* Buzzer alerts
* EEPROM configuration

---

## 🧪 Security and Monitoring Features

The project demonstrates multiple embedded security concepts:

* Multi-factor-style access options through stored and temporary PIN mechanisms
* Local authentication
* Physical tamper monitoring
* Environmental condition monitoring
* Non-volatile configuration storage
* Wireless monitoring
* Automatic lock recovery
* Real-time alert generation
* Human-machine interface through OLED and keypad

---

## 🎯 Applications

Potential applications include:

* Forensic evidence storage
* Controlled-access document storage
* Research laboratory sample storage
* Secure lockers
* Academic embedded-security demonstrations
* IoT security prototypes
* Controlled-access storage systems

---

## 🔮 Future Enhancements

Potential future improvements include:

* 📲 Dedicated Android/iOS monitoring application
* ☁️ Cloud-based sensor and event logging
* 📡 GSM/LTE emergency notifications
* 📷 Camera-based access/event recording
* 👆 Fingerprint authentication
* 🙂 Facial authentication
* 🔋 Battery backup and power monitoring
* 📝 Tamper-event history with timestamps
* 🔐 Improved credential/key management
* 🌐 Web-based remote monitoring dashboard

---

## 🧠 Technologies Used

**Embedded & IoT**

* Arduino Framework
* Embedded C/C++
* Microcontroller programming
* Sensor integration
* GPIO
* I²C communication
* Serial communication

**Sensors & Hardware**

* BME680
* MPU6050
* SH1106 OLED
* 4×4 Matrix Keypad
* HC-05 Bluetooth
* Relay
* Buzzer

**Software & Libraries**

* Arduino IDE
* U8G2
* Adafruit BME680
* Adafruit Unified Sensor
* Keypad
* SoftwareSerial
* MPU6050
* EEPROM

---

## 👥 Project Team

* **Hutthesh BS**
* **Hitesh Gowda H**
* **Ojus A**
* **Vinod Kumar V**

---

## ⚠️ Project Status

**Prototype / Academic Project**

This project was developed as an academic embedded/IoT security prototype. The current implementation demonstrates the core concepts of electronic access control, environmental sensing, tamper detection, Bluetooth communication, and persistent configuration.

It should undergo additional hardware, security, reliability, and field testing before being considered for production deployment.

---

## 📄 License

This project is intended for **educational, academic, and demonstration purposes**.

You may modify and extend the project with appropriate attribution to the original project team.
