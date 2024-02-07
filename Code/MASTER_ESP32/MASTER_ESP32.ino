
/*
  Using I2C to send and receive structs between two ESP32s
  SDA is the data connection and SCL is the clock connection
  SCL 22
  SDA 21
  GNDs must also be connected
*/

#include <Wire.h>
//
//#define I2C_DEV_ADDR 0x55
//
struct Zone
{
  int16_t volume;
  bool enabled;
};

Zone zone1;


void setup()
{
  // Set up serial
  Serial.begin(115200);

  // Set up I2C
  Wire.begin();
}

void loop()
{
  // Request Data every 50 ms
//  Serial.println("Requesting data");
//  Wire.requestFrom(8, sizeof(zone1));
//  Wire.readBytes((byte *)&zone1, sizeof(zone1));
//  Serial.println(zone1.volume);
//  Serial.println(zone1.enabled);
//  delay(50);
  zone1.enabled = false;
  zone1.volume = 0;
  Wire.beginTransmission(8);
  Wire.write((byte *)&zone1, sizeof(zone1));
  Wire.endTransmission();
  Serial.println("1");
  delay(5000);
  zone1.enabled = true;
  zone1.volume = 50;
  Wire.beginTransmission(8);
  Wire.write((byte *)&zone1, sizeof(zone1));
  Wire.endTransmission();
  Serial.println("2");
  delay(5000);
  

}
