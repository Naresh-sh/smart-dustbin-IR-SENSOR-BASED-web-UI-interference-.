# smart-dustbin-IR-SENSOR-BASED-web-UI-interference-.
Smart Dustbin (ESP8266, AP Mode)  A smart, Wi-Fi-enabled dustbin with smooth servo control, IR sensing, real-time monitoring, and a sleek web interface — fully self-contained as a local hotspot. Designed for hobbyists, makers, and IoT enthusiasts.

Smart Dustbin (ESP8266, AP Mode)

A smart, Wi-Fi-enabled dustbin with smooth servo control, IR sensing, real-time monitoring, and a sleek web interface — fully self-contained as a local hotspot. Designed for hobbyists, makers, and IoT enthusiasts.

Features

Standalone Wi-Fi AP mode: Connect directly to ESP8266 without an external router.

Web UI (password protected): Monitor and control your dustbin via browser at http://192.168.4.1.

IR sensor detection: Automatically opens the lid when trash is detected.

Smooth servo movement: Avoids jerky motions for longevity and silent operation.

Fill level simulation: Tracks how full the dustbin is (5 opens = +2% fill, capped at 100%).

EEPROM persistence: Open count, close count, and fill level are saved even after power-off.

Real-time time display: Syncs time via NTP when available.

Fast UI polling: Updates every 500ms to show live status.

JSON API: Access /status, /open, /close endpoints for integration with other systems.

Clean, dark-themed web interface: Includes advanced info and “Powered by Naresh” footer.

Hardware

MCU: ESP8266 (NodeMCU, Wemos D1 Mini, etc.)

Servo motor: Connected to D4 (GPIO2)

IR sensor: Connected to D5 (GPIO14), detects trash presence

Power: 5V USB or regulated supply

Setup & Usage

Flash the provided firmware to ESP8266 using Arduino IDE.

Connect servo and IR sensor to the correct GPIO pins.

Power on ESP8266 — it will create a Wi-Fi hotspot Dustbin_AP (password: 12345678).

Open browser at http://192.168.4.1 and login with the same password.

Control the dustbin manually or let it operate automatically via IR sensor.

Check live open/close counts, fill level, and last open time.

EEPROM Persistence

The project ensures that:

Fill level, open count, and close count are retained even after reboot.

Simulated fill logic: every 5 lid openings → +2% fill.

Web API

GET /status → Returns JSON with current lid state, fill level, counts, and last open time.

GET /open → Opens the lid (increments open count).

GET /close → Closes the lid (increments close count).














