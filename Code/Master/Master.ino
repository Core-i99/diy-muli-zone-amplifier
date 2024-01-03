
/*
  Using I2C to send and receive structs between two ESP32s
  SDA is the data connection and SCL is the clock connection
  SCL 22
  SDA 21
  GNDs must also be connected
*/

#include <Wire.h>
#include <ArduinoHA.h>
#include <WiFi.h>

#define I2C_DEV_ADDR 0x55
#define BROKER_ADDR IPAddress(192, 168, 0, 21)
#define WIFI_SSID "Boshoek44c-verdiep-2.4GHz"
#define WIFI_PASSWORD "Boshoek44c1"

WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);

HASwitch zone1("zone1");
HANumber zone1Volume("zone1_volume");

struct I2cVolume
{
  int volume;
  bool enabled;
};

I2cVolume volume1;

// timing variables
unsigned long prevUpdateTime = 0;
unsigned long updateInterval = 500;

void setup()
{
  // Unique ID of the device
  byte mac[6];
  WiFi.macAddress(mac);
  device.setUniqueId(mac, sizeof(mac));

  // Set up serial
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  // Set device's details
  device.setName("ESP32");
  device.setSoftwareVersion("1.0.0");

  // Set up entities
  zone1.setName("Zone 1");
  zone1.onCommand(onZone1SwitchCommand);
  zone1Volume.setName("Zone 1 Volume");
  zone1Volume.setMin(0);
  zone1Volume.setMax(100);
  zone1Volume.setStep(1);
  zone1Volume.setMode(HANumber::ModeSlider);
  zone1Volume.onCommand(onZone1VolumeCommand);
  zone1Volume.setState(0);

  // Connect to MQTT
  mqtt.begin(BROKER_ADDR, "stijn", "Fs19fendt");

  // Set up I2C
  Wire.begin();
}

void loop()
{
  mqtt.loop();
  if (millis() - prevUpdateTime >= updateInterval)
  {
    prevUpdateTime = millis();
    Serial.println("Requesting new data!");
    byte stop = true;
    byte numBytes = 8;
    Wire.requestFrom(I2C_DEV_ADDR, numBytes, stop);
    // the request is immediately followed by the read for the response
    Wire.readBytes((byte *)&volume1, numBytes);
    Serial.println("New data received!");
    zone1Volume.setState(volume1.volume);
    zone1.setState(volume1.enabled);
    // Serial.print("New data Volume: ");
    // Serial.print(volume1.volume);
    // Serial.print(" enabled: ");
    // Serial.println(volume1.enabled);
  }
}

void onZone1SwitchCommand(bool state, HASwitch *sender)
{
  Serial.println("Zone 1 switch command received");
  Serial.print("State: ");
  Serial.println(state);
  volume1.enabled = state;

  // Write to I2C
  Wire.beginTransmission(I2C_DEV_ADDR);
  Wire.write((byte *)&volume1, sizeof(volume1));
  Wire.endTransmission();

  sender->setState(state); // report back to HA
}

void onZone1VolumeCommand(HANumeric number, HANumber *sender)
{
  Serial.println("Zone 1 volume command received");
  Serial.print("Volume: ");
  volume1.volume = number.toInt8();
  Serial.println(volume1.volume);

  // Write to I2C
  Wire.beginTransmission(I2C_DEV_ADDR);
  Wire.write((byte *)&volume1, sizeof(volume1));
  Wire.endTransmission();

  sender->setState(number); // report back to HA
}
