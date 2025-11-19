/*
Slave: Zone controller - Attiny44
I2C addresses:
  Zone 1: 8
  Zone 2: 9
  Zone 3: 10
Flash instructions: see SLAVE_FLASHING.md
*/

#include <Wire.h>
#include <DigiPotX9Cxxx.h>
#include <RotaryEncoder.h>

const int I2C_ADDR = 10; // 8, 9, 10
const int ROTARYMIN = 0;
const int ROTARYMAX = 200;
const int RO_IN_SW = 0;
const int RO_IN_DT = 2;
const int RO_IN_CLK = 1;
const int ZONE_ENABLE = 3;
const int POT_INC = 7;
const int POT_CS = 8;
const int POT_UD = 9;

// Rotary Button variables
int buttonState;
int lastButtonState = LOW;  // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

struct Zone
{
  int16_t volume;
  uint8_t enabled;  // Use uint8_t instead of bool for better control (0 or 1)
};

volatile Zone zone1 = {0, 0};

DigiPot pot(POT_INC, POT_UD, POT_CS);
RotaryEncoder encoder(RO_IN_CLK, RO_IN_DT, RotaryEncoder::LatchMode::TWO03);

// Pin for enabled
void setup()
{
  Wire.begin(I2C_ADDR);
  Wire.onRequest(requestEvent);
  // Wire.onReceive(receiveEvent); // Disabled - not currently needed
  pot.reset();
  pinMode(ZONE_ENABLE, OUTPUT);
  pinMode(RO_IN_SW, INPUT);
  encoder.setPosition(0);
};

void loop()
{
  // Rotary encoder button
  int rotaryButtonReading = digitalRead(RO_IN_SW);
  if (rotaryButtonReading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (rotaryButtonReading != buttonState) {
      buttonState = rotaryButtonReading;

      // only toggle the zone status if the new button state is LOW
      if (buttonState == LOW) {
        noInterrupts(); // Disable interrupts during write
        zone1.enabled = !zone1.enabled;
        interrupts(); // Re-enable interrupts
        setVolume(); // Update relay immediately
      }
    }
  }

  lastButtonState = rotaryButtonReading;



  // Rotary encoder rotation
  encoder.tick(); 
  int newPos = encoder.getPosition();

  if (newPos < ROTARYMIN) {
    encoder.setPosition(ROTARYMIN);
    newPos = ROTARYMIN;

  } else if (newPos > ROTARYMAX) {
    encoder.setPosition(ROTARYMAX);
    newPos = ROTARYMAX;
  }
  else {
    int16_t newVolume = int(newPos / 2);
    noInterrupts(); // Protect the write from I2C interrupts
    zone1.volume = newVolume;
    interrupts();
    setVolume();
  }
}

void requestEvent()
{
  // Create a temporary copy - read volatiles into local variables
  // This is safe because each member is read atomically on AVR (8-bit and 16-bit reads)
  Zone tempZone;
  tempZone.volume = zone1.volume;
  tempZone.enabled = zone1.enabled;
  Wire.write((byte *)&tempZone, sizeof(tempZone));
}

// void receiveEvent(int bytes)
// {
//   for (int i = 0; i < sizeof(zone1); i++) {
//     ((uint8_t*)&zone1)[i] = Wire.read();
//   }
//   encoder.setPosition(zone1.volume * 2);
//   setVolume();
// }

void setVolume()
{
  // Map the volume level to the potentiometer value, which is between 0 and 99
  // But we need to invert the volume level, because a potentiometer value of 0 is the highest volume level
  int potlevel = map(100 - zone1.volume, 0, 100, 0, 99);

  // Set the potentiometer value
  pot.set(potlevel);

  // Set the enabled pin
  digitalWrite(ZONE_ENABLE, !zone1.enabled);
}
