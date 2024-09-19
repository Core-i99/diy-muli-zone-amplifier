/*
  This example draws fonts (as used by the Adafruit_GFX library) onto the
  screen. These fonts are called the GFX Free Fonts (GFXFF) in this library.

  Other True Type fonts could be converted using the utility within the
  "fontconvert" folder inside the library. This converted has also been
  copied from the Adafruit_GFX library.

  Since these fonts are a recent addition Adafruit do not have a tutorial
  available yet on how to use the utility.   Linux users will no doubt
  figure it out!  In the meantime there are 48 font files to use in sizes
  from 9 point to 24 point, and in normal, bold, and italic or oblique
  styles.

  This example sketch uses both the print class and drawString() functions
  to plot text to the screen.

  Make sure LOAD_GFXFF is defined in the User_Setup.h file within the
  TFT_eSPI library folder.

  #########################################################################
  ###### DON'T FORGET TO UPDATE THE User_Setup.h FILE IN THE LIBRARY ######
  #########################################################################
*/

#include <TFT_eSPI.h> // Hardware-specific library
#include "Free_Fonts.h" // Include the header file attached to this sketch
#include "WiFi.h"
#include "Audio.h"
#include <Wire.h>
#include <RotaryEncoder.h>
#include <ArduinoHA.h>

// I2S module pins
#define I2S_DOUT 17
#define I2S_BCLK 0
#define I2S_LRC 16

// Rotary encoder pins
#define ROT_DT 34
#define ROT_CLK 39
#define ROTARYMIN 0
#define ROTARYMAX 100

// WIFI
String ssid = "Boshoek44c-verdiep-2.4GHz";
String password = "Boshoek44c1";

// I2C ADDR
#define I2C_ZONE1_ADDR 0x55
#define I2C_ZONE2_ADDR 0x56
#define I2C_ZONE3_ADDR 0x57

// MQTT
#define MQTT_BROKER_IP IPAddress(192, 168, 0, 21)
const String MQTT_BROKER_USER = "stijn";
const String MQTT_BROKER_PASS = "password";

// 
TFT_eSPI tft = TFT_eSPI();                   // Invoke custom library with default width and height
Audio audio;
RotaryEncoder *encoder = nullptr;
//WiFiClient client;
//HADevice device;
//HAMqtt mqtt(client, device);

// Home Assistant
//HASwitch zone1Switch("zone1");
//HANumber zone1Volume("zone1_volume");
//HASwitch zone2Switch("zone1");
//HANumber zone2Volume("zone1_volume");
//HASwitch zone3Switch("zone1");
//HANumber zone3Volume("zone1_volume");

// Zone struct
struct Zone
{
  int16_t volume;
  bool enabled;
};
Zone zone1;
Zone zone2;
Zone zone3;

// Rotary Encoder interrupt
IRAM_ATTR void checkPosition()
{
  encoder->tick(); // just call tick() to check the state.
}

void setup(void) {
  // Serial setup
  Serial.begin(115200);
 
  // Unique ID
//  byte mac[6];
//  WiFi.macAddress(mac);
//  device.setUniqueId(mac, sizeof(mac));

  // WIFI
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED)
    delay(1500);

  // Home assistant
//  device.setName("Versterker");
//  device.setSoftwareVersion("1.0.0");
//  zone1Switch.setName("Zone 1");
//  zone1Volume.setName("Zone 1 Volume");
//  zone1Volume.setMin(0);
//  zone1Volume.setMax(100);
//  zone1Volume.setStep(1);
//  zone1Volume.setMode(HANumber::ModeSlider);
//  zone1Volume.setState(0);
//  zone2Switch.setName("Zone 2");
//  zone2Volume.setName("Zone 2 Volume");
//  zone2Volume.setMin(0);
//  zone2Volume.setMax(100);
//  zone2Volume.setStep(1);
//  zone2Volume.setMode(HANumber::ModeSlider);
//  zone2Volume.setState(0);
//  zone3Switch.setName("Zone 3");
//  zone3Volume.setName("Zone 3 Volume");
//  zone3Volume.setMin(0);
//  zone3Volume.setMax(100);
//  zone3Volume.setStep(1);
//  zone3Volume.setMode(HANumber::ModeSlider);
//  zone3Volume.setState(0);
//    mqtt.begin(BROKER_ADDR, MQTT_BROKER_USER, MQTT_BROKER_PASS);
  
  // I2C
  Wire.begin();

  // Rotary encoder
  encoder = new RotaryEncoder(ROT_DT, ROT_CLK, RotaryEncoder::LatchMode::TWO03);
  attachInterrupt(digitalPinToInterrupt(ROT_DT), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROT_CLK), checkPosition, CHANGE);
  
  // I2S
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolumeSteps(100); // max 255
  audio.setVolume(15);
  audio.connecttohost("http://icecast.vrtcdn.be/mnm-high.mp3");
  
  // TFT setup
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  // Header
  tft.setTextColor(TFT_MAGENTA, TFT_BLUE);
  tft.fillRect(0, 0, 480, 80, TFT_BLUE);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("SOURCE: INTERNET RADIO", 239, 10, 4);
  // Header radio station
  tft.setTextColor(TFT_MAGENTA, TFT_BLUE);
  tft.drawString("ZENDER: MNM", 239, 50, 4);


  // Zones 
  tft.setTextColor(TFT_WHITE);
  tft.setFreeFont(FS24);
  // Write first row
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ZONE 1", 5, 100);
  setZoneStatus(1, true);
  setZoneVolume(1, 50);


  // Write second row
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ZONE 2", 5, 160);
  setZoneStatus(2, true);
  setZoneVolume(2, 50);

  // Write third row
  tft.setTextDatum(TL_DATUM);
  tft.drawString("ZONE 3", 5, 220);
  setZoneStatus(3, true);
  setZoneVolume(3, 0);


  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // TFT Backlight
  pinMode(32, OUTPUT);
  digitalWrite(32, HIGH);
}

void loop() {
  // Rotary encoder
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
    Serial.println(newpos);
  }

  // I2S
  audio.loop();
}

void setZoneStatus(int zone, bool status){
  Serial.println("Set zone status: " + String(zone) + " value: " + String(status));
  tft.setTextDatum(TL_DATUM);
  if (status == true){
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("ON   ", 200, getZoneY(zone));
  }
  else{
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("OFF", 200, getZoneY(zone));
  }
  if (zone == 1){
    zone1.enabled = status;
  }
  else if (zone == 2){
    zone2.enabled = status;
  }
  else if (zone == 3){
    zone3.enabled = status;
  }
}

void setZoneVolume(int zone, int volume){
  Serial.println("Set zone volume: " + String(zone) + " value: " + String(volume));
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Reset the background
  tft.drawString("       %", 474, getZoneY(zone));
  tft.drawString(String(volume)+"%", 474, getZoneY(zone));
  if (zone == 1){
    zone1.volume = volume;
  }
  else if (zone == 2){
    zone2.volume = volume;
  }
  else if (zone == 3){
    zone3.volume = volume;
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
