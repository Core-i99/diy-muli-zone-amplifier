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
#include "AudioTools.h"
#include "AudioCodecs/CodecMP3Helix.h"

const char *urls[] = {
    "http://icecast.vrtcdn.be/mnm-high.mp3"};

// Wifi
const char *wifi = "Boshoek44c-verdiep-2.4GHz";
const char *password = "Boshoek44c1";

// Zone Variables
int zone1_vol = 0;
int zone2_vol = 0;
int zone3_vol = 0;
bool zone1_status = false;
bool zone2_status = false;
bool zone3_status = false;

TFT_eSPI tft = TFT_eSPI();                   // Invoke custom library with default width and height
URLStream urlStream(wifi, password);
AudioSourceURL source(urlStream, urls, "audio/mp3");
I2SStream i2s;
MP3DecoderHelix decoder;
AudioPlayer player(source, i2s, decoder);

void setup(void) {
  // Serial setup
  Serial.begin(115200);

  // Audio setup
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = 0;
  config.pin_ws = 10;
  config.pin_data = 11;
  i2s.begin(config);

  // setup player
  player.begin();

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
  // updateVolume(); // remove comments to activate volume control
  // updatePosition();  // remove comments to activate position control
  player.copy();
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
    zone1_status = status;
  }
  else if (zone == 2){
    zone2_status = status;
  }
  else if (zone == 3){
    zone3_status = status;
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
    zone3_vol = volume;
  }
  else if (zone == 2){
    zone2_vol = volume;
  }
  else if (zone == 3){
    zone3_vol = volume;
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
