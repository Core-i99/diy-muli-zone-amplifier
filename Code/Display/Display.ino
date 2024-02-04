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
#include <SPI.h>
#include "Free_Fonts.h" // Include the header file attached to this sketch
#include <RotaryEncoder.h>
#include <debounce.h>

// Rotary encoder variables
const int rotaryMin = 0;                    // Set rotary encoder min, max and steps var
const int rotaryMax = 50;
const int rotarySteps = 2;
const int zone3RotaryIn1 = 25;
const int zone3RotaryIn2 = 26;
const int zone3ButtonPin = 27;

// Zone Variables
int zone1_vol = 0;
int zone2_vol = 0;
int zone3_vol = 0;
bool zone1_status = false;
bool zone2_status = false;
bool zone3_status = false;

void buttonHandler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    if (btnId == 3){
      setZoneStatus(3, !zone3_status);
    }
  }
}

TFT_eSPI tft = TFT_eSPI();                   // Invoke custom library with default width and height
RotaryEncoder *zone3Encoder = nullptr;           // A pointer to the dynamic created rotary encoder instance. This will be done in setup
static Button zone3Button(3, buttonHandler);   // button with id 3

IRAM_ATTR void zone3CheckPosition()
{
  zone3Encoder->tick();
}

void setup(void) {
  // Serial setup
  Serial.begin(115200);


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
  tft.drawString("ZENDER: RADIO 2 ANT", 239, 50, 4);


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


  // Rotary encoder setup
  zone3Encoder = new RotaryEncoder(zone3RotaryIn1, zone3RotaryIn2, RotaryEncoder::LatchMode::TWO03);
  attachInterrupt(digitalPinToInterrupt(zone3RotaryIn1), zone3CheckPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(zone3RotaryIn2), zone3CheckPosition, CHANGE);

  // Button setup
  pinMode(zone3ButtonPin, INPUT_PULLUP);
}

void loop() {
  // Button stuff
  pollButtons();

  // Rotary encoder stuff
  int zone3Position = zone3Encoder->getPosition();
  if(zone3Position > rotaryMax){
    zone3Encoder->setPosition(rotaryMax);
    zone3Position = rotaryMax;
  }
  else if(zone3Position < rotaryMin){
    zone3Encoder->setPosition(rotaryMin);
    zone3Position = rotaryMin;
  }
  else{
    // Serial.print("Position");
    // Serial.println(zone3Position * rotarySteps);
    if (zone3_vol != zone3Position * rotarySteps){
      Serial.println("Setting zone volume");
      setZoneVolume(3, zone3Position * rotarySteps);
      zone3_vol = zone3Position * rotarySteps;
    }
  }
}

void pollButtons() {
  zone3Button.update(digitalRead(zone3ButtonPin));
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
