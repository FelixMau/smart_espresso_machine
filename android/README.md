# Shot Control — Android app

A phone remote and shot journal for the smart espresso machine. It talks to the
ESP32's JSON API over the local network (no cloud, no account, no extra service).

## What it does

**Live tab** — everything the built-in web page offers, plus the curve:

- weight, pressure, shot time, predicted end, pump duty, scale link, WiFi signal
- Start / Stop the shot
- set the goal weight
- set the weight offset (the value the machine also learns by itself)
- the extraction curve as it is recorded

**Journal tab** — every shot that was pulled, kept on the phone:

- automatically saved a few seconds after the shot ends (once the scale settles)
- weight, duration, peak pressure, average flow, time to first drip
- the full curve, scrubbable: touch the chart to read weight and pressure at any second
- a rating and a note per shot (grinder setting, bean, how it tasted)
- export one shot as CSV or the whole journal as JSON

**Settings tab**

- machine address, with mDNS discovery ("Find on network")
- panel mode (below)
- the pressure profile editor (points by elapsed time and by time remaining)
- polling interval and auto-save

## Panel mode

For an old phone mounted permanently on the machine: dark when nothing is
happening, awake when coffee is.

- **Goes dark** after N minutes (1/2/5/10/30, default 5) of no touch. Never
  while a shot is running, whatever the timer says.
- **Wakes** when the scale connects — the earliest signal that coffee is about
  to happen, since the Acaia sleeps on its own — or on a touch.
- **Two dark styles:**
  - *Black screen* (default): a black surface with the backlight at zero. No
    permissions, tap anywhere to wake. Genuinely no light on OLED; a faint glow
    on LCD.
  - *Display off*: really switches the display off via Device Admin `lockNow()`.
    You grant device admin once, in Settings; the app requests only `force-lock`
    and uses it for nothing else. If admin is revoked it falls back to the black
    screen rather than getting stuck lit.

A **foreground service** (with a quiet, badge-less notification) runs while panel
mode is on. It is not optional: when the display is off Android stops the
Activity, and something has to stay alive to notice the scale. It also relaunches
the panel with `setTurnScreenOn` when a wake trigger fires.

Polling backs off to every 5 s while dark — slow enough to be cheap, fast enough
that the screen is up before you have locked the portafilter in.

If the phone is on a charger, the display is the only thing that really costs
anything here; the WiFi polling is the background drain, and that is what the
5 s dark interval is for.

## Building

Requires JDK 17 and the Android SDK (platform 35). Easiest path is Android
Studio: **File → Open →** this `android/` folder, then Run.

From the command line:

```bash
cd android
./gradlew assembleDebug            # apk in app/build/outputs/apk/debug/
./gradlew installDebug             # onto a connected device
```

If Gradle cannot find the SDK, point it there once:

```bash
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
```

## Connecting to the machine

1. Put the WiFi credentials in `firmware/src/secrets.h` and flash the firmware
   (`platformio run -d firmware -e upesy_wroom --target upload`).
2. The ESP32 announces itself over mDNS as `espresso.local`.
3. In the app open **Settings → Find on network**, or type the IP the serial
   monitor printed at boot.

Android does not resolve `*.local` names through its normal resolver, which is
why the app discovers the service and stores the numeric address it resolved to.
If discovery is blocked on your network (guest WiFi, client isolation), the IP
address always works.

## The API it speaks

Implemented in [`../firmware/src/webserver.cpp`](../firmware/src/webserver.cpp). All responses are JSON;
the dashboard's own endpoints (`/state`, `/start_shot`, …) and the built-in HTML
page still work unchanged.

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | all live values in one request |
| `GET /api/shot?from=N` | recorded curve of the current/last shot, `from` skips samples already held |
| `GET /api/profile` | the current pressure profile |
| `GET /api/start`, `/api/stop` | brew control (deferred to the main loop, never blocks the web task) |
| `GET /api/set_goal_weight?value=` | goal weight, persisted to EEPROM |
| `GET /api/set_weight_offset?value=` | drip offset, persisted to EEPROM |
| `GET /api/set_pressure_profile?times=&pressures=` | negative times mean "seconds left" |

`/api/shot` returns points as `[time_s, weight_g, pressure_bar]` triples, which
is why the app can mirror a shot while it is still being pulled and only ever
asks for the samples it does not have yet.

## Notes on the design

- **No database and no HTTP library.** One JSON file per shot in the app's
  private storage, `HttpURLConnection` + `org.json` for the wire. The machine is
  one hop away and a shot is a few hundred points; the dependencies would cost
  more than they buy.
- **Two stacked panels, not one plot with two y-axes.** A shared time axis keeps
  "pressure dropped, flow picked up" readable without inviting the reader to
  infer crossings between grams and bar that do not mean anything.
- **Polling pauses when the app is not on screen**, and runs slower when no shot
  is in progress, so the ESP32 is not hammered while it idles.
