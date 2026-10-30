# Arduino CAN Cluster

This project integrates Arduino Uno, a CAN module, and a BMW E90 car dashboard to visualize game data using the SimHub application.

---

## Table of Contents

1. [Project Description](#project-description)
2. [Requirements](#requirements)
3. [Installation](#installation)
4. [ESP32 Version (Bluetooth)](#esp32-version-bluetooth)
5. [Usage](#usage)
6. [Features](#features)
7. [License](#license)
8. [Contact](#contact)

---

## Project Description

* The project allows reading game data and displaying it on a physical BMW E90 car dashboard.
* Uses Arduino Uno as the control unit, a module based on the MCP2515, and the SimHub application for configuration and data transfer.
* Includes Arduino code and a SimHub configuration file.
* Can be part of a driving simulator or an educational platform for learning CAN Bus.

---

## Requirements

* Arduino Uno
* CAN Bus module (e.g., MCP2515)
* CAN Bus-compatible car dashboard
* Software:

  * Arduino IDE to upload code to Arduino
  * SimHub application for configuration and data transfer
* Project files:

  * Arduino code (`.ino` file)
  * SimHub configuration (`.shsds` file)

---

## Installation

Step-by-step instructions to set up and run the project:

1. Clone the repository:

   ```bash
   git clone https://github.com/Adam-Sidor/CAN_Cluster
   ```
2. Upload the code to Arduino:

   * Open the `.ino` file in Arduino IDE.
   * Install the required libraries.

     * If you are using MCP2515 module and don't have the CAN library, copy the `CAN-0.3.1` folder into your Arduino libraries folder (on Windows usually in `Documents`).
   * Select the correct COM port and Arduino model.
   * Upload the code to the board.
3. Configure SimHub:

   * Open the SimHub application.
   * Create a new device under `Custom serial devices`.
   * Import the project configuration file using `Import settings`.
   * Configure the connection with Arduino under `Serial settings`.
4. Connect the hardware:

   * Connect the CAN module to Arduino and the car dashboard.
   * Connect power `+12V` and ground to all boards.
   * Connect Arduino via USB cable.
   * To remove the low ambient temperature message, add a resistor with appropriate resistance, e.g., `6 kΩ`. The temperature on the dashboard depends on the chosen resistor. With this value, the displayed temperature is about 18 ℃.
   * Wiring diagram:
     ![Schematic](images/schematic.png)

---

## ESP32 Version (Bluetooth)

The `CAN_Cluster_ESP32` folder contains a version for ESP32 that receives data from SimHub wirelessly over Bluetooth (SPP serial port) and talks to the dashboard through the same MCP2515 module.

* Requires the classic **ESP32** (e.g., ESP32-WROOM / DevKit V1). ESP32-S2/S3/C3/C6 chips do not support Bluetooth Classic.
* The MCP2515 driver is included in the sketch folder - the `CAN-0.3.1` library is not needed (on ESP32 it uses the built-in controller instead of MCP2515).
* Wiring the MCP2515 module to ESP32:

  | MCP2515 | ESP32 |
  |---|---|
  | VCC | 5V (VIN) |
  | GND | GND |
  | CS | GPIO5 |
  | SCK | GPIO18 |
  | SI (MOSI) | GPIO23 |
  | SO (MISO) | GPIO19 through a voltage divider (e.g., 1 kΩ from the module, 2 kΩ to GND) |
  | INT | not connected (only used for receiving) |

  The module must be powered from 5 V (required by the TJA1050 transceiver). 3.3 V signals from ESP32 are at the edge of the MCP2515 input threshold when it runs at 5 V - it usually works, but if the serial monitor shows `Connecting with CAN failed!`, check the wiring and the MISO divider.
* On-board computer buttons (BC, up, down) are not on CAN - in the car they connect cluster pin 16 to ground through 1 kΩ (BC), 2 kΩ (up) or 3 kΩ (down). They can be controlled from ESP32 with three N-channel MOSFETs (e.g., 2N7000), optional:

  | ESP32 | MOSFET | Resistor to cluster pin 16 |
  |---|---|---|
  | GPIO25 | gate (BC) | 1 kΩ |
  | GPIO26 | gate (up) | 2 kΩ |
  | GPIO27 | gate (down) | 3 kΩ |

  Source of each MOSFET goes to GND (common with the cluster), drain through its resistor to pin 16. A 100 kΩ resistor from each gate to GND keeps the buttons released while ESP32 boots. Do not connect GPIO directly to pin 16. Three ordinary push buttons with the same resistors work as well.
  Commands: `bc1` / `bc2` / `bc3` press BC / up / down for 200 ms, `bcHold1` / `bcHold2` / `bcHold3` for 2 s (long press).
* Check control messages and warning lights are sent in CAN frame `0x592`. The `ccOn<ID>` / `ccOff<ID>` commands turn a message on/off (active ones are repeated every second, otherwise the cluster drops them). The SimHub configuration maps ETS2/ATS warnings:

  | Game warning | CC-ID |
  |---|---|
  | Low fuel | 275 |
  | AdBlue | 643 |
  | Oil pressure | 212 |
  | Water temperature | 39 |
  | Battery voltage | 213 |
  | Air pressure | 308 |
  | Engine damage ≥ 10 % | 31 (check engine) |
  | Transmission damage ≥ 10 % | 288 (gearbox) |
  | Wheels damage ≥ 10 % | 50 (tyre pressure) |
  | Cabin or chassis damage ≥ 10 % | 21 |

  Other IDs tested on the cluster: 28 (oil level), 166 (coolant level), 169 (yellow exclamation mark). The cluster confirms a shown message in frame `0x338` (visible with `DEBUG_CAN_RX`). The "!" service reminder (CC-ID 281) shown after every ignition on is turned off continuously (`SUPPRESS_SERVICE_LIGHT`). IDs come from [veikkos/e90-can-cluster](https://github.com/veikkos/e90-can-cluster).
* Fuel consumption (`fuelRate<µl/s>`) is sent as total injected fuel in the engine data frame `0x1D0`, the cluster computes the consumption gauge from it. The gauge works only together with DME status `0x12F` and vehicle speed from DSC `0x1A0`, which are sent as well. In the SimHub configuration it is divided by 3, so truck consumption fits the car gauge - remove `/ 3` for cars.
* After uploading the code, pair the computer with the `CAN_Cluster` device. Windows will create a COM port (Bluetooth, outgoing) - select it under `Serial settings` in SimHub. The SimHub configuration is the same as for Arduino.
* When Bluetooth disconnects, the speedometer and tachometer drop to zero.
* Debugging tools (USB serial monitor, 115200 baud, line ending `Newline`):

  * With `DEBUG_CAN_RX` enabled, frames sent by the cluster itself are printed as `CAN< ID [length] data`. Each ID is printed at most once per 2 s, `0x338` (check control messages) also on every change. Requires the MISO line to be connected.
  * The `can<ID>:<data>[x<count>]` command (ID and data in hex) sends any frame to the cluster, e.g. `can1EE:40FF` sends it once and `can1EE:40FFx20` 20 times every 100 ms; `x0` stops a repeated frame. It works over Bluetooth too and is meant for finding unknown frames, such as the BC button on the turn signal stalk.

---

## Usage

1. Start Arduino and ensure the device is correctly connected.
2. Configure SimHub to read data from the selected game and communicate with Arduino.
3. Observe real-time data on the car dashboard.

---

## Features

Some features may not work depending on the game!

* Integration with SimHub.
* Data visualization on the car dashboard:

  * Speedometer
  * Tachometer
  * Fuel gauge (may require pressing the Arduino RESET button)
  * Clock displaying computer system time
  * Handbrake indicator
  * Turn signals and hazard lights
  * Truck warnings (low fuel, AdBlue, oil pressure, water temperature, battery, air pressure) and damage as check control messages (ESP32 version, ETS2/ATS)
  * Fuel consumption gauge (ESP32 version)
  * Cruise control set speed shown on the display while cruise is active (ESP32 version, ETS2/ATS; km/h shown as mph 1:1) - this also removes the cruise control error shown after power-up
  * Lights:

    * Parking lights
    * Low beam (front fog light indicator)
    * High beam
    * Warning lights (rear fog light indicator)
    * Dashboard backlight according to active lights
  * Temperature indicator (fixed value depending on resistor selected during [Installation](#installation))

---

## License

All rights reserved. The project is provided for demonstration and educational purposes only.

* You may use this project for personal and educational purposes.
* Commercial use or redistribution of the project in whole or in part requires explicit written consent from the author.

---

## Contact

* Author: Adam Sidor
* Email: [sidoadsi1@gmail.com](mailto:sidoadsi1@gmail.com)
