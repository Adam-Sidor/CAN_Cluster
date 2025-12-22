# Arduino CAN Cluster

This project integrates Arduino Uno, a CAN module, and a BMW E90 car dashboard to visualize game data using the SimHub application.

---

## Table of Contents

1. [Project Description](#project-description)
2. [Requirements](#requirements)
3. [Installation](#installation)
4. [Usage](#usage)
5. [Features](#features)
6. [License](#license)
7. [Contact](#contact)

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

     * If you don't have the CAN library, copy the `CAN-0.3.1` folder into your Arduino libraries folder (on Windows usually in `Documents`).
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
  * Turn signals
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
