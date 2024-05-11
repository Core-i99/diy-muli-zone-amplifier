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

const char *wifi = "Orange-f47d9";
const char *password = "Rbua5ww3";

URLStream urlStream(wifi, password);
AudioSourceURL source(urlStream, urls, "audio/mp3");
I2SStream i2s;
MP3DecoderHelix decoder;
AudioPlayer player(source, i2s, decoder);

#define AUDIO_INPUT_SELECTOR_P1 25
#define AUDIO_INPUT_SELECTOR_P2 33

void setup()
{
  Serial.begin(115200);
  AudioLogger::instance().begin(Serial, AudioLogger::Info);

  // setup output
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = 0;
  config.pin_ws = 16;
  config.pin_data = 17;

  i2s.begin(config);

  // setup player
  player.begin();

  pinMode(AUDIO_INPUT_SELECTOR_P1, OUTPUT);
  pinMode(AUDIO_INPUT_SELECTOR_P2, OUTPUT);
  digitalWrite(AUDIO_INPUT_SELECTOR_P1, 0);
  digitalWrite(AUDIO_INPUT_SELECTOR_P2, 0);
}

void loop()
{
  // updateVolume(); // remove comments to activate volume control
  // updatePosition();  // remove comments to activate position control
  player.copy();
}