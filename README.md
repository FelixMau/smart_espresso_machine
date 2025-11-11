### smart espresso machine
This is a more or less private project where I work on my Espresso machine to digitalise and IOT enable my espresso machine.
#### Planned features
Planed features include:
- [x] Connect the scale to the microcontroller
- [ ] Microcontroller can read the *shotbutton* from the machine
- [ ] Microcontroller is able to also 'press' the *shotbutton*
- [x] Design a PCB to hold the sensors
- [ ] Microcontroller is able to adjust pump power via a Dimmer
- [x] Mechanical button/knob to *manually* adjust Pump power during shots
- [ ] Add pressure sensor
- [ ] Pressure seonsor readings are shown on a simple display.
- [ ] Design cases for PCB, display, knob

#### Working details of Features
- Connection of Scale to Microcontroller
  - Scale is a Acaia Lunar scale and I am using @tatemazer's [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) Library
  - Microcontroller is a ESP32 Board
- Button is read via Optocoppler just like in [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) Library as well.
- Button is written via Optocoppler just like in [AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) Library too.
- Microcontroller is using a PWM signal to control the Dimmer, unfortunately I have not been able to get the zero crossing functionality running.
- Encoder knob is connected to control PumpPower as the PCB is faulty and I needed to be able to keep dringing cofe while work is in progress.

#### New PCB design almost ready for production:
- [x] Added lights Power connections
- [x] Correct false routing
- [x] Correct Footprint of Optokopplers
- [x] Adding on/off switch to save power on ESP
- [ ] Get second opintion on PCB design.
