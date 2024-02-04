// -----
// InterruptRotator.ino - Example for the RotaryEncoder library.
// This class is implemented for use with the Arduino environment.
//
// Copyright (c) by Matthias Hertel, http://www.mathertel.de
// This work is licensed under a BSD 3-Clause License. See http://www.mathertel.de/License.aspx
// More information on: http://www.mathertel.de/Arduino
// -----
// 18.01.2014 created by Matthias Hertel
// 04.02.2021 conditions and settings added for ESP8266
// 03.07.2022 avoid ESP8266 compiler warnings.
// 03.07.2022 encoder instance not static.
// -----

// This example checks the state of the rotary encoder using interrupts and in the loop() function.
// The current position and direction is printed on output when changed.

// Hardware setup:
// Attach a rotary encoder with output pins to
// * 2 and 3 on Arduino UNO. (supported by attachInterrupt)
// * A2 and A3 can be used when directly using the ISR interrupts, see comments below.
// * D5 and D6 on ESP8266 board (e.g. NodeMCU).
// Swap the pins when direction is detected wrong.
// The common contact should be attached to ground.
//
// Hints for using attachinterrupt see https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/

#include <RotaryEncoder.h>


// Example for ESP8266 NodeMCU with input signals on pin D5 and D6
#define PIN_IN1 25
#define PIN_IN2 26
#define ROTARYMIN 0
#define ROTARYMAX 50
#define ROTARYSTEPS 2


// A pointer to the dynamic created rotary encoder instance.
// This will be done in setup()
RotaryEncoder *encoder = nullptr;

IRAM_ATTR void checkPosition()
{
  encoder->tick(); // just call tick() to check the state.
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("InterruptRotator example for the RotaryEncoder library.");

  encoder = new RotaryEncoder(PIN_IN1, PIN_IN2, RotaryEncoder::LatchMode::TWO03);

  // register interrupt routine
  attachInterrupt(digitalPinToInterrupt(PIN_IN1), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN2), checkPosition, CHANGE);
}


// Read the current position of the encoder and print out when changed.
void loop()
{
  int newpos = encoder->getPosition();
  if(newpos > ROTARYMAX){
    encoder->setPosition(ROTARYMAX);
    newpos = ROTARYMAX;
  }
  else if (newpos < ROTARYMIN){
    encoder->setPosition(ROTARYMIN);
    newpos = ROTARYMIN;
  }
  else{
    Serial.print("Position:");
    Serial.println(newpos * ROTARYSTEPS);
  }
  
}