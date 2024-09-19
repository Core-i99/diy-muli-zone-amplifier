/*
  Using I2S to play audio from a URL
*/

// WIFI
#include "WiFi.h"
#include "Audio.h"

// Digital I/O used
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC 26

Audio audio;

String ssid = "Boshoek44c-verdiep-2.4GHz";
String password = "Boshoek44c1";

void setup()
{
  Serial.begin(115200);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED)
    delay(1500);
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolumeSteps(100); // max 255
  audio.setVolume(50);

  //  *** radio streams ***
  audio.connecttohost("http://icecast.vrtcdn.be/mnm-high.mp3");
}
void loop()
{
  audio.loop();
}