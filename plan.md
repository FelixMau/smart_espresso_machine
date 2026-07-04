# Plan: LAN Web Dashboard

## Goal

A web page, reachable over the local network, that lets me monitor and control
the espresso machine while it brews — without slowing down the real-time control
(scale reads, trajectory calc, pump PWM).

## Confirmed scope

**Live monitoring** (browser polls the ESP):
- Brewing state (idle / brewing)
- Pressure: current reading + current goal setpoint
- PID values (P / I / D terms and output)
- Shot time
- Cup weight (from the Acaia scale)
- Pump power (%)

**Editable from the web page:**
- Pressure profile by time (the existing time→pressure curve that feeds the PID setpoint)
- Single goal cup weight
- Weight offset

**Full control from the web page:**
- Start / stop a shot
- Live PID tuning: Kp / Ki / Kd sliders (adjustable while brewing, for dialing in)

**Shot history:**
- Keep the last ~5 shots in RAM (lost on reboot — acceptable)
- Downsample the 1000-point trajectory arrays to ~50–100 points to save memory
- The raw trajectory of the last completed shot already lives in
  `shot.weight[]`/`shot.time_s[]` until the next shot starts (`datapoints` only
  resets at shot start), so history is just a snapshot+downsample taken at shot
  end, before the next shot overwrites it.
- Plot live + historical shots with Chart.js loaded from CDN
- Per-shot "key findings": final weight, duration, end reason, peak pressure

## Current state / gaps

- Two half-built UIs exist, both disabled (commented out in main.cpp):
  - `webserver.h` — working AsyncWebServer with real endpoints (`/weight`,
    `/pressure`, `/set_goal_weight`, `/set_pressure_profile`, …) + a plain page.
    This is the usable foundation to build on.
  - `SuperMon.h` — nicer HTML template (start/stop, offset slider) but polls a
    `/json` endpoint that doesn't exist and isn't wired to any server.
- **PID values are not exposed** — `PIDController` keeps its terms private.
- **Pump power is not exposed** — it's the local `finalPwmValue` in `loop()`.

## Technical constraints

- **WiFi + BLE share one radio.** Running the Acaia scale (BLE) and the web
  server (WiFi) at the same time is the main risk. main.cpp currently runs at
  `setCpuFrequencyMhz(80)` — likely needs restoring to 240 MHz for headroom.
- **Keep the BLE poll hot.** `scale.newWeightAvailable()` must be called
  continuously or the scale connection drops.
- **Memory is tight.** `Shot` already holds `weight[1000]` + `time_s[1000]`
  (~8 KB). Shot history must be downsampled — but downsampled it's tiny
  (~5 shots × 100 pts × 2 floats ≈ 4 KB), so RAM-only history fits comfortably.

## Implementation steps (build order)

1. **Expose missing data.** Add getters to `pid_controller.h` for the P/I/D
   terms and last output; publish pump power and current goal pressure into the
   `Shot` struct so the page can read them. (Low risk, no behavior change.)

2. **Single JSON state endpoint.** One `GET /state` returning everything at once
   (less CPU per browser refresh than many small endpoints). Add endpoints for
   PID tuning, start/stop, goal weight, weight offset, and pressure profile.

3. **Shot history buffer.** Ring buffer of ~5 downsampled shots, snapshotted from
   `shot.weight[]`/`shot.time_s[]` at shot end (before the next shot overwrites
   them); `GET /shots` (list + key findings) and `GET /shot?id=N` (trajectory).

4. **Dashboard page.** Build out the dashboard page (live tiles, editors,
   sliders, Chart.js plots from CDN), consolidating the two existing UI stubs.
   Serve from PROGMEM initially; move to LittleFS later if iterating on the HTML
   by reflashing becomes a nuisance.

5. **Concurrency & stability.** Restore 240 MHz, move scale polling / trajectory
   / PWM into FreeRTOS tasks so the web server can't stall the control loop, and
   wire WiFi + server startup back into `main.cpp`. Riskiest part.
   - Fallback if WiFi+BLE proves flaky: serve the page only when idle and buffer
     live data, instead of fully concurrent.

## Biggest risk

Step 5 — BLE scale + WiFi sharing the radio. Test on hardware incrementally.
Try concurrent first (live-during-shot monitoring is wanted); fall back to
idle-only serving if needed.
