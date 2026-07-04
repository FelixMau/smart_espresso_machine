# smart espresso machine

A hobby project to digitalise and IoT-enable my **Dalla Corte Mini** espresso machine (and maybe other machines in the future).

Unlike the [Gaggiuino Project](https://gaggiuino.github.io/#/?id=home), the goal here is **not** to replace the machine's original controls but to **extend** them. This way we don't have to mingle so much with high voltages, and the machine stays fully restorable to stock.

An ESP32 reads a Bluetooth scale and a pressure sensor, drives the pump via a PWM dimmer with a PID pressure controller, and uses linear regression to predict when a shot reaches its target weight — stopping extraction at the optimal moment. Everything is monitored and controlled live from a web dashboard on the LAN.

## Dashboard

Live monitoring, start/stop/reset, PID tuning, goal weight and pressure profile editing, plus live charts and a shot-history comparison — all served from the ESP32.

<!-- Add real screenshots to the assets/ folder (see assets/README.md) -->
![Dashboard](assets/dashboard.png)
![Live shot](assets/dashboard.gif)

## Features

- [x] Connect the scale to the microcontroller
- [x] Microcontroller can read the *shot button* from the machine
- [x] Microcontroller is able to also 'press' the *shot button*
- [x] Design a PCB to hold the sensors
- [x] Microcontroller adjusts pump power via a dimmer
- [x] Mechanical knob to *manually* adjust pump power during shots
- [x] Add pressure sensor
- [x] PID-controlled pressure profiles (live-tunable over the web)
- [x] Predictive shot stopping via linear regression on weight-vs-time
- [x] EEPROM auto-learning of the weight offset after each shot
- [x] Async web dashboard: live tiles, charts, control and tuning
- [ ] Pressure sensor readings shown on a simple on-device display
- [ ] Design cases for PCB, display, knob

## Working details

- **Scale**: Acaia Lunar, via [@tatemazer's AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) library.
- **Microcontroller**: ESP32 (upesy_wroom), firmware built with PlatformIO.
- **Button read/write**: via optocouplers, so the ESP never touches the machine's high-voltage logic.
- **Pump control**: PWM signal to the dimmer (zero-crossing functionality not yet working).
- **Manual override**: an encoder knob controls pump power (handy while work is in progress).
- **Architecture**: separate FreeRTOS tasks for BLE, the control loop, and the web server, so a missing scale or busy web client can never stall brewing. Boots and brews without WiFi and without the scale.

## Getting started

```bash
# 1. Provide WiFi credentials (this file is gitignored — never commit it)
cp src/secrets.h.example src/secrets.h   # then edit ssid / password

# 2. Build and upload
platformio run -e upesy_wroom
platformio run -e upesy_wroom --target upload

# 3. Watch serial output (dashboard URL is printed periodically)
platformio device monitor
```

The dashboard is then reachable at the IP the ESP32 prints on the serial console.

## PCB

The KiCad design lives in `pcb/`. Latest revision:

- [x] Added lights / power connections
- [x] Corrected false routing
- [x] Corrected optocoupler footprints
- [x] Added on/off switch to save power on the ESP
- [x] Got a second opinion on the PCB design
- [x] Added interface for a potential display

## License

Licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

Copyright © 2026 Felix Maurer.
