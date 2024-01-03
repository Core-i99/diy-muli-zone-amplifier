/*
  Using I2C to send and receive structs between two ESP32s
  SDA is the data connection and SCL is the clock connection
  SCL 22
  SDA 21
  GNDs must also be connected
*/

/*
  X9C103P: 10K digital potentiometer
  1: INC : 26
  2: U/D : 27
  3: Vh
  4: Vss
  5: Vw
  6: Vl
  7: CS : 28
  8: VCC
*/

#include <Wire.h>
#include <DigiPotX9Cxxx.h>

#define I2C_DEV_ADDR 0x55

struct I2cVolume
{
  int volume;
  bool enabled;
};

I2cVolume volume = {0, false};

DigiPot pot(25, 26, 27); // INC, U/D, CS

// Pin for enabled
const int ENABLED_PIN = 33;

void setup()
{
  Serial.begin(115200);
  Wire.begin(I2C_DEV_ADDR);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent);
  pot.reset();
  pinMode(ENABLED_PIN, OUTPUT);
};

void loop()
{
}

void requestEvent()
{
  Serial.println("Requested data");
  Wire.write((byte *)&volume, sizeof(volume));
}

void receiveEvent(int bytes)
{
  Serial.println("Received data");
  while (Wire.available())
  {
    Wire.readBytes((byte *)&volume, sizeof(volume));
    Serial.print("Volume level: ");
    Serial.print(volume.volume);
    Serial.print(" Enabled: ");
    Serial.println(volume.enabled);
    setVolume();
  }
}

void setVolume()
{
  // Map the volume level to the potentiometer value, which is between 0 and 99
  // But we need to invert the volume level, because a potentiometer value of 0 is the highest volume level
  int potlevel = map(100 - volume.volume, 0, 100, 0, 99);
  Serial.print("Potentiometer level: ");
  Serial.println(potlevel);

  // Set the potentiometer value
  pot.set(potlevel);

  // Set the enabled pin
  digitalWrite(ENABLED_PIN, volume.enabled);
}