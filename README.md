# Home-Alert-System

The Home-Alert-System is a small, cost-effective embedded system designed to improve home or small office safety by monitoring for intrusions and unusual environmental conditions. **Please note: This is designed as a "leave the home" system**, intended to be armed when the premises are unoccupied. 

When sensor thresholds are exceeded, the system provides real-time safety monitoring by triggering alerts via notifications or alarms to a central base station.

## Features

The system is capable of detecting:
* **Movement:** Identifying potential intruders.
* **Sounds:** Detecting unusual audio activity.
* **Smoke or Gas Leakage:** Alerting users to environmental hazards.

## System Architecture

The project utilizes **Raspberry Pi Pico 2** boards to separate the sensing logic from the core processing. 

### Sensor Node
* Collects environmental and proximity data from multiple hardware sensors.
* Handles hardware interrupts for immediate threshold events.
* Transmits packaged data to the base node via I2C or UART.

### Base Node
* Powered by the **Zephyr RTOS**.
* Provides a Sensors API interface for the sensor node using a custom Zephyr sensor driver.
* Receives incoming data and triggers external alerts (e.g., LED warnings or console notifications).

## Hardware & Sensors

The following sensors are integrated into the Sensor Node:

| Sensor | Function | Connection Type | Notes |
| :--- | :--- | :--- | :--- |
| **HC-SR04** | Motion / Object proximity | Digital | Detects physical intrusions. Operates at 5V (requires a voltage divider for the Pico 2). |
| **BME680** | Smoke, gas, temperature, pressure, humidity | Digital (I2C) | Primary monitor for environmental hazards and air quality. |
| **MCP9700AE** | Analog temperature | Analog | Used to cross-check and validate BME680 temperature readings. |
