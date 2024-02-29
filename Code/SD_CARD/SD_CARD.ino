#include <TFT_eSPI.h>
#include "Free_Fonts.h"
#include "WiFi.h"
#include <ArduinoJson.h>
#include "SD.h"
#include "Audio.h"
#include <Wire.h>

// Zone Controller I2C ADDR
#define ZONE1_I2C_ADDR 8
#define ZONE2_I2C_ADDR 9
#define ZONE3_I2C_ADDR 10

// I2S module pins
#define I2S_DOUT 17
#define I2S_BCLK 0
#define I2S_LRC 16

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

unsigned long lastI2cTime = 0;
unsigned long i2cTimeDelay = 50;


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
  
  // Connect to internet radio (I2S)
  startupTFT("Verbinden met internet radio");
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolumeSteps(100); // max 255
  audio.setVolume(15);
  audio.connecttohost(config.radioStations[0].url);

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

  // Read from Zone Controllers (I2C)
  if ((millis() - lastI2cTime) > i2cTimeDelay) {
    int16_t zone1_vol_old = zone1.volume;
    bool zone1_enabled_old = zone1.enabled;
    int16_t zone2_vol_old = zone2.volume;
    bool zone2_enabled_old = zone2.enabled;
    int16_t zone3_vol_old = zone3.volume;
    bool zone3_enabled_old = zone3.enabled;
    Serial.println("Zone 1 i2c");
    Wire.requestFrom(ZONE1_I2C_ADDR, sizeof(zone1));
    Wire.readBytes((byte *)&zone1, sizeof(zone1));
    Serial.println("Zone 2 i2c");
    Wire.requestFrom(ZONE2_I2C_ADDR, sizeof(zone2));
    Wire.readBytes((byte *)&zone2, sizeof(zone2));
    Serial.println("Zone 3 i2c");
    Wire.requestFrom(ZONE3_I2C_ADDR, sizeof(zone3));
    Wire.readBytes((byte *)&zone3, sizeof(zone3));
    if (zone1.enabled != zone1_enabled_old){
      setZoneStatus(1);
    }
    if (zone1.volume != zone1_vol_old){
      setZoneVolume(1);
    }
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
    lastI2cTime = millis();
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

