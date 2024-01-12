/*
  ESP32 internet radio using I2S
*/

#include "AudioTools.h"
#include "AudioCodecs/CodecMP3Helix.h"

const char *urls[] = {
    "http://stream.srg-ssr.ch/m/rsj/mp3_128",
    "http://stream.srg-ssr.ch/m/drs3/mp3_128",
    "http://stream.srg-ssr.ch/m/rr/mp3_128",
    "http://sunshineradio.ice.infomaniak.ch/sunshineradio-128.mp3",
    "http://streaming.swisstxt.ch/m/drsvirus/mp3_128"};

const char *wifi = "Boshoek44c-verdiep-2.4GHz";
const char *password = "Boshoek44c1";

URLStream urlStream(wifi, password);
AudioSourceURL source(urlStream, urls, "audio/mp3");
I2SStream i2s;
MP3DecoderHelix decoder;
AudioPlayer player(source, i2s, decoder);

void setup()
{
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Info);

  // setup output
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = 27;
  config.pin_ws = 26;
  config.pin_data = 25;

  i2s.begin(config);

  // setup player
  player.begin();
}

void loop()
{
  // updateVolume(); // remove comments to activate volume control
  // updatePosition();  // remove comments to activate position control
  player.copy();
}