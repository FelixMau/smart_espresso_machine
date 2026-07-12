// Guard name must NOT be WEBSERVER_H: ESPAsyncWebServer checks that macro to
// detect the core WebServer library and would skip defining HTTP_GET etc.
#ifndef ESPRESSO_WEBSERVER_H
#define ESPRESSO_WEBSERVER_H

// ============================================================================
// LAN WEB DASHBOARD SERVER
// ============================================================================
// Async web server for monitoring and controlling the machine while it brews.
// All HTTP handlers run in the AsyncTCP task, NOT the control task. Handlers
// therefore never touch the scale/BLE directly: start/stop requests are set
// as flags that the control task picks up on its next iteration.
// Implementations live in webserver.cpp.

#include <Arduino.h>

#include "pid_controller.h"

extern bool wifiConnected;
extern bool serverStarted;  // Lets loop() start the server if WiFi comes up late

// Set by HTTP handlers, consumed by the control task
extern volatile bool webStartRequest;
extern volatile bool webStopRequest;
extern volatile bool webResetRequest;  // Stop ESP/scale shot without pressing the machine button
extern volatile bool webRebootRequest; // Consumed by loop(); refused while brewing/cleaning

// Connect to WiFi with a timeout so a missing network can't hang boot forever.
// Returns true if connected; the web server is only started when it is.
bool initializeWiFi();

// Register all routes and start the server. The PID controller is exposed
// for live monitoring and tuning via /state and /set_pid.
void initializeServer(PIDController* pid);

#endif // ESPRESSO_WEBSERVER_H
