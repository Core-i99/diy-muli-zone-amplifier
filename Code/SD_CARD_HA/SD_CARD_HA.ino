#include <TFT_eSPI.h>
#include "Free_Fonts.h"
#include "WiFi.h"
#include <ArduinoJson.h>
#include "SD.h"
#include "Audio.h"
#include <Wire.h>
#include <RotaryEncoder.h>

// Zone Controller I2C ADDR
#define ZONE1_I2C_ADDR 8
#define ZONE2_I2C_ADDR 9
#define ZONE3_I2C_ADDR 10

// I2S module pins
#define I2S_DOUT 17
#define I2S_BCLK 0
#define I2S_LRC 16

// Audio input selector
#define AUDIO_INPUT_SELECTOR_P1 25
#define AUDIO_INPUT_SELECTOR_P2 33
uint8_t current_audio_input = 0;
uint8_t current_radio_station = 0;
uint8_t number_of_radio_stations = 0;

// Rotary encoder
#define RO_IN1 39
#define RO_IN2 34
#define RO_IN_SW 35
int roState;
int lastRoState = LOW;
unsigned long roLastDebounceTime = 0;
unsigned long roDebounceDelay = 50;

// MQTT
#define MQTT_BROKER_IP IPAddress(192, 168, 0, 21)
const String MQTT_BROKER_USER = "stijn";
const String MQTT_BROKER_PASS = "password";

struct Config
{
  char ssid[64];
  char pass[64];
  struct RadioStation
  {
    char name[32];
    char url[128];
  };
  RadioStation* radioStations;
};

struct Zone
{
  int16_t volume;
  bool enabled;
};

Config config;
TFT_eSPI tft = TFT_eSPI();
Audio audio;
Zone zone1;
Zone zone2;
Zone zone3;
RotaryEncoder *encoder = nullptr;
WiFiClient client;
HADevice device;
HAMqtt mqtt(client, device);

Home Assistant
HASwitch zone1Switch("zone1");
HANumber zone1Volume("zone1_volume");
HASwitch zone2Switch("zone1");
HANumber zone2Volume("zone1_volume");
HASwitch zone3Switch("zone1");
HANumber zone3Volume("zone1_volume");

unsigned long lastI2cTime = 0;
unsigned long i2cTimeDelay = 50;

IRAM_ATTR void roCheckPosition()
{
  encoder->tick(); // just call tick() to check the state.
}

void startupTFT(const char* message, bool error=false)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); // Middle centre
  tft.setFreeFont(FS24);
  tft.drawString("OPSTARTEN", 239, 159);
  tft.setFreeFont(FS9);
  if (error){ 
    tft.setTextColor(TFT_RED, TFT_BLACK);
  }
  tft.drawString(message, 239, 200);
  if (error) { // Don't go to next stage if error occurs
    while (1) {};
  }
}

void setAudioInput(){
  tft.setTextColor(TFT_MAGENTA, TFT_BLUE);
  tft.fillRect(0, 0, 480, 80, TFT_BLUE);
  tft.setTextDatum(TC_DATUM);
  
  if (current_audio_input == 0){
    digitalWrite(AUDIO_INPUT_SELECTOR_P1, LOW);
    digitalWrite(AUDIO_INPUT_SELECTOR_P2, LOW); 
    tft.drawString("INVOER: INTERNET RADIO", 239, 10, 4);
    setRadioStation();
  }
  else if (current_audio_input == 1) {
    digitalWrite(AUDIO_INPUT_SELECTOR_P1, LOW);
    digitalWrite(AUDIO_INPUT_SELECTOR_P2, HIGH); 
    tft.drawString("INVOER: CD1", 239, 30, 4);
  }
  else if (current_audio_input == 2) {
    digitalWrite(AUDIO_INPUT_SELECTOR_P1, HIGH);
    digitalWrite(AUDIO_INPUT_SELECTOR_P2, LOW); 
    tft.drawString("INVOER: CD2", 239, 30, 4);
  }
  else if (current_audio_input == 3) {
    digitalWrite(AUDIO_INPUT_SELECTOR_P1, HIGH);
    digitalWrite(AUDIO_INPUT_SELECTOR_P2, HIGH); 
    tft.drawString("INVOER: CD3", 239, 30, 4);
  }
}

void setRadioStation(){
  audio.setConnectionTimeout(500, 500); // Set connection timeout 500 http, 500 ssl
  audio.connecttohost(config.radioStations[current_radio_station].url);
  if (current_audio_input == 0){
    // Write to TFT
    tft.fillRect(0, 40, 480, 40, TFT_BLUE);
    tft.setTextColor(TFT_MAGENTA, TFT_BLUE);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("ZENDER: " + String(config.radioStations[current_radio_station].name), 239, 50, 4);
  }
}

void setZoneStatus(int zone){
  Serial.println("Set zone status: " + String(zone));
  tft.setTextDatum(TL_DATUM);
  if (zone == 1){
    if (zone1.enabled){
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("AAN   ", 200, getZoneY(zone));
    }
    else{
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("UIT   ", 200, getZoneY(zone));
    }
  }
  else if (zone == 2){
    if (zone2.enabled){
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("AAN   ", 200, getZoneY(zone));
    }
    else{
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("UIT   ", 200, getZoneY(zone));
    }
  }
  else if (zone == 3){
    if (zone3.enabled){
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("AAN   ", 200, getZoneY(zone));
    }
    else{
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("UIT   ", 200, getZoneY(zone));
    }
  }
}

void setZoneVolume(int zone){
  Serial.println("Set zone volume: " + String(zone));
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Reset the background
  tft.drawString("       %", 474, getZoneY(zone));
  if (zone == 1){
    tft.drawString(String(zone1.volume )+"%", 474, getZoneY(zone));
  }
  else if (zone == 2){
    tft.drawString(String(zone2.volume )+"%", 474, getZoneY(zone));
  }
  else if (zone == 3){
    tft.drawString(String(zone3.volume )+"%", 474, getZoneY(zone));
  }
}

int getZoneY(int zone){
  if (zone == 1) {
     return 100;
  }
  else if (zone == 2) {
    return 160;
  }
  else if (zone == 3){
    return 220;
  }
}

void setup()
{
  // Setup Serial
  Serial.begin(115200);

  // Setup display
  pinMode(32, OUTPUT);
  digitalWrite(32, HIGH);
  tft.begin();
  tft.setRotation(3);
  startupTFT("Lezen van SD kaart");

  // Read SD Card configuration file
  if (!SD.begin(13)) // SD_CS = 13
  {
    Serial.println("Card Mount Failed");
    SD.end();
    startupTFT("Fout bij het lezen van de SD kaart", true);
  }
  File file = SD.open("/config.json");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error){
    file.close();
    SD.end();
    startupTFT("Fout bij het lezen van het configuratie bestand", true);
  }
  
  strlcpy(config.ssid, doc["WIFI-SSID"], sizeof(config.ssid));
  strlcpy(config.pass, doc["WIFI-PASS"], sizeof(config.pass));
  JsonObject radioStations = doc["RADIO-STATIONS"].as<JsonObject>();
  config.radioStations = new Config::RadioStation[radioStations.size()];
  int index = 0;
  for (JsonPair station : radioStations)
  {
    // Get the key and value
    const char *stationName = station.key().c_str();
    const char *stationURL = station.value().as<const char *>();
    // Copy the station name and URL to the configuration object   
    strlcpy(config.radioStations[index].name, stationName, sizeof(config.radioStations[index].name));
    strlcpy(config.radioStations[index].url, stationURL, sizeof(config.radioStations[index].url));
    index++;
  }
  number_of_radio_stations = index;
  file.close();
  SD.end();

  // Connect to wifi
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.pass);
  startupTFT("Verbinden met WIFI");
  int wifiTries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    wifiTries++;
    if (wifiTries > 10){
      startupTFT("Kan niet verbinden met WIFI", true);
    }
  }
  // Setup audio inut selector
  pinMode(AUDIO_INPUT_SELECTOR_P1, OUTPUT);
  pinMode(AUDIO_INPUT_SELECTOR_P2, OUTPUT);

  // Connect to internet radio (I2S)
  startupTFT("Verbinden met internet radio");
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolumeSteps(100); // max 255
  audio.setVolume(15);

  // Connect to zone controllers (I2C)
  startupTFT("Verbinden met zone controllers");
  Wire.begin();
  Serial.println("Zone 1 i2c");
  Wire.requestFrom(ZONE1_I2C_ADDR, sizeof(zone1));
  Wire.readBytes((byte *)&zone1, sizeof(zone1));
  Serial.println("Zone 2 i2c");
  Wire.requestFrom(ZONE2_I2C_ADDR, sizeof(zone1));
  Wire.readBytes((byte *)&zone2, sizeof(zone2));
  Serial.println("Zone 3 i2c");
  Wire.requestFrom(ZONE3_I2C_ADDR, sizeof(zone1));
  Wire.readBytes((byte *)&zone3, sizeof(zone3));
  Serial.println("I2C Finished");

  // MQTT (Home Assistant)
  byte mac[6];
  WiFi.macAddress(mac);
  device.setUniqueId(mac, sizeof(mac));
  device.setName("Versterker");
  device.setSoftwareVersion("1.0.0");
  zone1Switch.setName("Zone 1");
  zone1Switch.onCommand(onZone1SwitchCommand);
  zone1Volume.setName("Zone 1 Volume");
  zone1Volume.setMin(0);
  zone1Volume.setMax(100);
  zone1Volume.setStep(1);
  zone1Volume.setMode(HANumber::ModeSlider);
  zone1Volume.onCommand(onZone1VolumeCommand);
  zone1Volume.setState(0);
  zone2Switch.setName("Zone 2");
  zone2Switch.onCommand(onZone2SwitchCommand);
  zone2Volume.setName("Zone 2 Volume");
  zone2Volume.setMin(0);
  zone2Volume.setMax(100);
  zone2Volume.setStep(1);
  zone2Volume.setMode(HANumber::ModeSlider);
  zone1Volume.onCommand(onZone2VolumeCommand);
  zone2Volume.setState(0);
  zone3Switch.setName("Zone 3");
  zone3Switch.onCommand(onZone3SwitchCommand);
  zone3Volume.setName("Zone 3 Volume");
  zone3Volume.setMin(0);
  zone3Volume.setMax(100);
  zone3Volume.setStep(1);
  zone3Volume.setMode(HANumber::ModeSlider);
  zone1Volume.onCommand(onZone3VolumeCommand);
  zone3Volume.setState(0);
  mqtt.begin(BROKER_ADDR, MQTT_BROKER_USER, MQTT_BROKER_PASS);

  // Rotary encoder
  pinMode(RO_IN_SW, INPUT);
  encoder = new RotaryEncoder(RO_IN1, RO_IN2, RotaryEncoder::LatchMode::TWO03);
  attachInterrupt(digitalPinToInterrupt(RO_IN1), roCheckPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RO_IN2), roCheckPosition, CHANGE);

  // Audio Input
  setAudioInput();

  // TFT Setup
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setFreeFont(FS24);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ZONE 1", 5, 100);
  setZoneStatus(1);
  setZoneVolume(1);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ZONE 2", 5, 160);
  setZoneStatus(2);
  setZoneVolume(2);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ZONE 3", 5, 220);
  setZoneStatus(3);
  setZoneVolume(3);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void loop()
{
  // Internet Radio (I2S)
  audio.loop();

  // Rotary encoder RO
  if (encoder->getPosition() > 1 || encoder->getPosition() < -1){
    encoder->setPosition(0);
    Serial.println("RO Position changed");
    if (current_radio_station == number_of_radio_stations -1){
      Serial.println("END of radio stations");
      current_radio_station = 0;
    }
    else {
      current_radio_station++;
    }
    setRadioStation();
  }

  // Rotary encoder SW
  int roReading = digitalRead(RO_IN_SW);
  if (roReading != lastRoState) {
    roLastDebounceTime = millis();
  }

  if ((millis() - roLastDebounceTime) < roDebounceDelay){
    // if the button state has changed:
    if (roReading != roState) {
      roState = roReading;
      if (roState == LOW) {
        Serial.println("Button pressed!");
        if (current_audio_input == 3){
          current_audio_input = 0;
        }
        else {
          current_audio_input++;
        }
        setAudioInput();
      }
    }
  }
  lastRoState = roReading;

  // // Read from Zone Controllers (I2C)
  if ((millis() - lastI2cTime) > i2cTimeDelay) {
    // int16_t zone1_vol_old = zone1.volume;
    // bool zone1_enabled_old = zone1.enabled;
    int16_t zone2_vol_old = zone2.volume;
    bool zone2_enabled_old = zone2.enabled;
    int16_t zone3_vol_old = zone3.volume;
    bool zone3_enabled_old = zone3.enabled;
    // Serial.println("Zone 1 i2c");
    // Wire.requestFrom(ZONE1_I2C_ADDR, sizeof(zone1));
    // Wire.readBytes((byte *)&zone1, sizeof(zone1));
    Serial.println("Zone 2 i2c");
    Wire.requestFrom(ZONE2_I2C_ADDR, sizeof(zone2));
    Wire.readBytes((byte *)&zone2, sizeof(zone2));
    Serial.println("Zone 3 i2c");
    Wire.requestFrom(ZONE3_I2C_ADDR, sizeof(zone3));
    Wire.readBytes((byte *)&zone3, sizeof(zone3));
    // if (zone1.enabled != zone1_enabled_old){
    //   setZoneStatus(1);
    // }
    // if (zone1.volume != zone1_vol_old){
    //   setZoneVolume(1);
    // }
    if (zone2.enabled != zone2_enabled_old){
      setZoneStatus(2);
    }
    if (zone2.volume != zone2_vol_old){
      setZoneVolume(2);
    }
    if (zone3.enabled != zone3_enabled_old){
      setZoneStatus(3);
    }
    if (zone3.volume != zone3_vol_old){
      setZoneVolume(3);
    }
    zone1Volume.setState(zone1.volume);
    zone1Switch.setState(zone1.enabled);
    zone2Volume.setState(zone2.volume);
    zone2Switch.setState(zone2.enabled);
    zone3Volume.setState(zone3.volume),
    zone3Switch.setState(zone3.enabled);
    lastI2cTime = millis();
  }
}


void onZone1SwitchCommand(bool state, HASwitch *sender)
{
  Serial.println("Zone 1 switch command received");
  Serial.print("State: ");
  Serial.println(state);
  zone1.enabled = state;
  // Write to I2C
  Wire.beginTransmission(ZONE1_I2C_ADDR);
  Wire.write((byte *)&zone1, sizeof(zone1));
  Wire.endTransmission();
  sender->setState(state); // report back to HA
}

void onZone1VolumeCommand(HANumeric number, HANumber *sender)
{
  Serial.println("Zone 1 volume command received");
  Serial.print("Volume: ");
  zone1.volume = number.toInt8();
  Serial.println(volume1.volume);
  // Write to I2C
  Wire.beginTransmission(ZONE1_I2C_ADDR);
  Wire.write((byte *)&zone1, sizeof(zone1));
  Wire.endTransmission();
  sender->setState(number); // report back to HA
}

void onZone2SwitchCommand(bool state, HASwitch *sender)
{
  Serial.println("Zone 2 switch command received");
  Serial.print("State: ");
  Serial.println(state);
  zone2.enabled = state;
  // Write to I2C
  Wire.beginTransmission(ZONE2_I2C_ADDR);
  Wire.write((byte *)&zone2, sizeof(zone2));
  Wire.endTransmission();
  sender->setState(state); // report back to HA
}

void onZone2VolumeCommand(HANumeric number, HANumber *sender)
{
  Serial.println("Zone 2 volume command received");
  Serial.print("Volume: ");
  zone2.volume = number.toInt8();
  Serial.println(volume2.volume);
  // Write to I2C
  Wire.beginTransmission(ZONE2_I2C_ADDR);
  Wire.write((byte *)&zone2, sizeof(zone2));
  Wire.endTransmission();
  sender->setState(number); // report back to HA
}

void onZone3SwitchCommand(bool state, HASwitch *sender)
{
  Serial.println("Zone 3 switch command received");
  Serial.print("State: ");
  Serial.println(state);
  zone3.enabled = state;
  // Write to I2C
  Wire.beginTransmission(ZONE3_I2C_ADDR);
  Wire.write((byte *)&zone3, sizeof(zone3));
  Wire.endTransmission();
  sender->setState(state); // report back to HA
}

void onZone3VolumeCommand(HANumeric number, HANumber *sender)
{
  Serial.println("Zone 3 volume command received");
  Serial.print("Volume: ");
  zone2.volume = number.toInt8();
  Serial.println(volume3.volume);
  // Write to I2C
  Wire.beginTransmission(ZONE3_I2C_ADDR);
  Wire.write((byte *)&zone3, sizeof(zone3));
  Wire.endTransmission();
  sender->setState(number); // report back to HA
}