# smart espresso machine

A hobby project to digitalise and IoT-enable my **Dalla Corte Mini** espresso machine (and maybe other machines in the future).

Unlike [Gaggiuino](https://gaggiuino.github.io/#/?id=home) or [GaggiMate](https://gaggimate.eu/), the goal here is **not** to replace the machine's original controls but to **extend** them. This way we don't have to mingle so much with high voltages, and the machine stays fully restorable to stock.

An ESP32 reads a Bluetooth scale and a pressure sensor, drives the pump via a PWM dimmer with a PID pressure controller, and uses linear regression to predict when a shot reaches its target weight — stopping extraction at the optimal moment. Everything is monitored and controlled live from a web dashboard on the LAN.

## How this compares to Gaggiuino & GaggiMate

Both projects turn cheap single-boiler machines (Gaggia Classic, Rancilio Silvia) into smart ones by taking over the heater, pump and steam control. This project serves a different niche:

| | Gaggiuino / GaggiMate | This project |
|---|---|---|
| Target machine | Single-boiler entry-level | Dual-boiler prosumer (Dalla Corte Mini) |
| Integration | Replaces the machine's controls | Extends them (opto-isolated button presses) — restorable to stock |
| Temperature & steam | PID heater + steam control | Not needed — the dual boiler already does this |
| Display | Installed touchscreen | Headless — a spare phone / any browser is the screen |
| Brew by weight | Yes | Yes, with regression-based predictive stop and auto-learned drip offset |
| Pressure profiling | Yes (Gaggiuino also flow) | Yes — time-based profiles with PID pump control |
| App integration | Beanconqueror (Gaggiuino) | Beanconqueror, via Gaggiuino-compatible API |

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
- [ ] Design import to beanconqueror -> custom machine
should be able to start shot via app
- [ ] Implement correct zero crosing detection and update pwm control accordingly
- [ ] Cleaning mode/cycle for descale and for detergent
- [ ] If no Wifi is available advertise own wifi and on it the webserver to do the same as on the local network
- [ ] Add another (manual)pressuresensor to evaluate accuracy of messurements, they sometimes feel a little off or I should adjust the expansionvalve

### Future tasks:

- [ ] Check on 230V safety and how to design properly with 230V on a espressomachine (that uses heats up water and puts it under pressure)
- [ ] Expose machine to internet? IDK this leads to many questions needing answeres such as how and which ports would need to be opened? Xiaomi robo cleaner does that somehow but I dont know how yet. Also password would be needed to log in -> or some local setup would provide a "password" via the app internally but all of this seems like a hustle for just the little benefit of no need to connect to local wifi....
- [ ] How could I sense the watertank fill status?



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
