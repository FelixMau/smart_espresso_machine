#include <Arduino.h>
#include <EEPROM.h>
#include "webserver.h"
#include "shotStopper.h"
#include "AcaiaArduinoBLE.h"
#include "SuperMon.h"
#include "secrets.h"

// Function prototypes
void initializeBluetoothScale();
void handleBluetoothScale();
void handleBrewingLogic();
void setBrewingState(bool brewing);
void setColor(int rgb[3]);
float seconds_f();
void calculateEndTime(Shot* currentShot);
void initializeServer(WebServer& server);
void handleClientRequests(WebServer& server);

void setup() {
    // Initialize serial communication
    Serial.begin(9600);

    // Initialize hardware and peripherals
    setCpuFrequencyMhz(80);
    EEPROM.begin(EEPROM_SIZE);

    // Initialize global variables
    goalWeight = EEPROM.read(WEIGHT_ADDR);
    weightOffset = EEPROM.read(OFFSET_ADDR) / 10.0;
    if (goalWeight < 10 || goalWeight > 200) goalWeight = 36;
    if (weightOffset > MAX_OFFSET) weightOffset = 1.5;
    Serial.print("Goal Weight retrieved: ");
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(IN, INPUT_PULLDOWN);
    pinMode(OUT, OUTPUT);
    pinMode(PIN_FAN, OUTPUT);
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_OUTPUT, OUTPUT);
    pinMode(PIN_A0, INPUT);
    Serial.print("Pins initialized");

    // Initialize Bluetooth scale
    initializeBluetoothScale();
    Serial.print("Bluetooth scale initialized");  

    // Initialize web server directly
    initializeServer(server);
    Serial.print("Web server initialized");

    // Initialize fan control
    ledcSetup(0, 10000, 8);
    ledcAttachPin(PIN_FAN, 0);
    ledcWrite(0, FanSpeed);

    // Disable watchdog timer on core 0
    disableCore0WDT();

    // Create BLE task
    xTaskCreatePinnedToCore(BLETask, "BLETask", 10000, NULL, 1, NULL, 0);
}

void loop() {
    // Debugging: Print free heap memory
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());

    // Handle Bluetooth scale
    handleBluetoothScale();

    // Handle brewing logic
    handleBrewingLogic();

    // Handle web server requests
    handleClientRequests(server);

    // Allow other tasks to run
    yield();
}

void initializeBluetoothScale() {
    BLE.begin();
    BLE.setLocalName("shotStopper");
    BLE.setAdvertisedService(weightService);
    weightService.addCharacteristic(weightCharacteristic);
    BLE.addService(weightService);
    weightCharacteristic.writeValue(goalWeight);
    BLE.advertise();
}

void handleBluetoothScale() {
    while (!scale.isConnected()) {
        scale.init();
        currentWeight = 0;
        if (shot.brewing) setBrewingState(false);
        if (scale.isConnected()) setColor(GREEN);
    }

    if (weightCharacteristic.written()) {
        goalWeight = weightCharacteristic.value();
        EEPROM.write(WEIGHT_ADDR, goalWeight);
        EEPROM.commit();
    }

    if (scale.heartbeatRequired()) scale.heartbeat();
    if (scale.newWeightAvailable()) {
        currentWeight = scale.getWeight();
        if (shot.brewing) {
            shot.time_s[shot.datapoints] = seconds_f() - shot.start_timestamp_s;
            shot.weight[shot.datapoints] = currentWeight;
            shot.shotTimer = shot.time_s[shot.datapoints];
            shot.datapoints++;
            calculateEndTime(&shot);
        }
    }
}

void handleBrewingLogic() {
    // Debugging: Print current weight and brewing state
    Serial.print("Current Weight: ");
    Serial.print(currentWeight);
    Serial.print(" g, Brewing State: ");
    Serial.println(shot.brewing ? "Brewing" : "Not Brewing");

    // Update sensor data
    auto sensorData = updateSensorData(SensorUpdate);
    int sensorBitsA0 = std::get<0>(sensorData);
    float sensorVoltsA0 = std::get<1>(sensorData);
    SensorUpdate = std::get<2>(sensorData);

    // Handle brewing state changes
    if (currentWeight >= (goalWeight - weightOffset) && shot.brewing) {
        shot.end = Shot::WEIGHT;
        setBrewingState(false);
    }
}