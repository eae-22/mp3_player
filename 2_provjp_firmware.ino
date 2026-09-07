#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// =============================
// SD CARD
// =============================
#define SD_CS   4
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

// =============================
// I2S - MAX98357A
// =============================
#define I2S_BCLK 14
#define I2S_LRC  27
#define I2S_DOUT 33

// =============================
// LED RING - WS2812B
// =============================
#define LED_PIN 22
#define NUM_LEDS 16

// =============================
// LED MATRIX - MAX7219
// =============================
#define MATRIX_DIN 13
#define MATRIX_CS  12
#define MATRIX_CLK 15

// =============================
// TOUCH MODULE
// GPIO34 nhận tín hiệu số từ module
// =============================
#define TOUCH_PIN 34

// =============================
// CONFIG
// =============================
#define MAX_FILES 1000
#define RING_EFFECT_COUNT 60
#define MATRIX_EFFECT_COUNT 60
#define RING_INTERVAL 30000UL

class FixedRateAudioOutput : public AudioOutputI2S {
public:
  bool SetRate(int rate) override {
    return AudioOutputI2S::SetRate(44100);
  }
};

AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
FixedRateAudioOutput *out = nullptr;

String mp3Files[MAX_FILES];
int fileCount = 0;
int currentFile = 0;

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int currentRingEffect = 0;
int currentMatrixPattern = 0;

unsigned long lastRingChange = 0;
unsigned long lastRingUpdate = 0;
unsigned long ringStep = 0;

bool songPending = false;
unsigned long songSwitchTime = 0;

bool lastTouchState = false;
bool stableTouchState = false;
unsigned long touchDebounceTime = 0;

// =====================================================
// MAX7219
// =====================================================

void max7219ShiftOut(byte data) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(MATRIX_CLK, LOW);
    digitalWrite(MATRIX_DIN, (data >> i) & 1);
    digitalWrite(MATRIX_CLK, HIGH);
  }
}

void max7219Send(byte address, byte data) {
  digitalWrite(MATRIX_CS, LOW);
  max7219ShiftOut(address);
  max7219ShiftOut(data);
  digitalWrite(MATRIX_CS, HIGH);
}

void clearMatrix() {
  for (int i = 0; i < 8; i++) {
    max7219Send(i + 1, 0);
  }
}

void setRowMatrix(int row, byte value) {
  if (row >= 0 && row < 8) {
    max7219Send(row + 1, value);
  }
}

void initMatrix() {
  pinMode(MATRIX_DIN, OUTPUT);
  pinMode(MATRIX_CLK, OUTPUT);
  pinMode(MATRIX_CS, OUTPUT);

  digitalWrite(MATRIX_CS, HIGH);
  digitalWrite(MATRIX_CLK, LOW);
  digitalWrite(MATRIX_DIN, LOW);

  max7219Send(0x0C, 0x01);
  max7219Send(0x0F, 0x00);
  max7219Send(0x09, 0x00);
  max7219Send(0x0B, 0x07);
  max7219Send(0x0A, 0x08);

  clearMatrix();
}

// =====================================================
// 60 PATTERN MATRIX
// =====================================================

const byte matrixPatterns[60][8] = {

  {0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x18},
  {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
  {0x3C,0x42,0xA5,0x81,0xA5,0x99,0x42,0x3C},
  {0x18,0x18,0x18,0xFF,0xFF,0x18,0x18,0x18},
  {0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xFF},
  {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40},
  {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01},
  {0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81},
  {0x18,0x24,0x42,0x81,0x81,0x42,0x24,0x18},
  {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55},

  {0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA},
  {0xF0,0x90,0x90,0xF0,0x90,0x90,0x90,0xF0},
  {0x3C,0x42,0x81,0x81,0x81,0x81,0x42,0x3C},
  {0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00},
  {0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,0xFF},
  {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00},
  {0x08,0x1C,0x3E,0x7F,0x3E,0x1C,0x08,0x00},
  {0x00,0x08,0x18,0x3F,0x3F,0x18,0x08,0x00},
  {0x00,0x10,0x18,0xFC,0xFC,0x18,0x10,0x00},
  {0x3C,0x7E,0xDB,0xFF,0xFF,0xDB,0x7E,0x3C},

  {0x18,0x66,0xC3,0x81,0x81,0xC3,0x66,0x18},
  {0x7E,0x81,0xA5,0x81,0xBD,0x81,0x81,0x7E},
  {0x00,0x3C,0x42,0x99,0x81,0x99,0x42,0x3C},
  {0x00,0x18,0x24,0x42,0x81,0x42,0x24,0x18},
  {0x81,0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3},
  {0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF},
  {0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF},
  {0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01},
  {0xFF,0xFE,0xFC,0xF8,0xF0,0xE0,0xC0,0x80},
  {0xFF,0x00,0x80,0x40,0x20,0x10,0x08,0x04},

  {0x04,0x08,0x10,0x20,0x40,0x80,0x00,0xFF},
  {0xFF,0x01,0x02,0x04,0x08,0x10,0x20,0x40},
  {0x40,0x20,0x10,0x08,0x04,0x02,0x01,0xFF},
  {0xFF,0x99,0x99,0xFF,0x99,0x99,0xFF,0x00},
  {0x00,0xFF,0x99,0x99,0xFF,0x99,0x99,0xFF},
  {0x18,0x18,0x7E,0xFF,0x18,0x18,0x18,0x18},
  {0x18,0x18,0x18,0x18,0xFF,0x7E,0x18,0x18},
  {0x10,0x18,0x1C,0xFE,0x1C,0x18,0x10,0x00},
  {0x08,0x18,0x38,0x7F,0x38,0x18,0x08,0x00},
  {0x18,0x3C,0x18,0x18,0x18,0x18,0x3C,0x18},

  {0x18,0x3C,0x7E,0x18,0x18,0x7E,0x3C,0x18},
  {0x81,0x42,0x24,0xFF,0xFF,0x24,0x42,0x81},
  {0x24,0x66,0xFF,0xFF,0xFF,0xFF,0x66,0x24},
  {0x00,0x18,0x3C,0x7E,0xFF,0x7E,0x3C,0x18},
  {0x18,0x3C,0x7E,0xFF,0x18,0xFF,0x7E,0x3C},
  {0xFF,0x81,0xBD,0xA5,0xA5,0xBD,0x81,0xFF},
  {0x00,0x7E,0x42,0x5A,0x5A,0x42,0x7E,0x00},
  {0x00,0x3C,0x7E,0xDB,0xDB,0x7E,0x3C,0x00},
  {0x18,0x24,0x7E,0xDB,0xFF,0xDB,0x7E,0x24},
  {0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x18},

  {0xFF,0xC3,0xA5,0x99,0x99,0xA5,0xC3,0xFF},
  {0x00,0x66,0xFF,0xFF,0xFF,0xFF,0x66,0x00},
  {0x81,0xC3,0xE7,0xFF,0xFF,0xE7,0xC3,0x81},
  {0x3C,0x66,0xC3,0x81,0x81,0xC3,0x66,0x3C},
  {0xFF,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFF},
  {0xFF,0x03,0x03,0x03,0x03,0x03,0x03,0xFF},
  {0x18,0x18,0x18,0xFF,0xFF,0x18,0x18,0x18},
  {0x81,0x81,0x42,0x24,0x18,0x24,0x42,0x81},
  {0x18,0x18,0x66,0x66,0xFF,0xFF,0x18,0x18},
  {0x00,0x18,0x3C,0x66,0x66,0x3C,0x18,0x00}
};

void displayMatrixPattern(int index) {
  index %= MATRIX_EFFECT_COUNT;

  for (int row = 0; row < 8; row++) {
    setRowMatrix(row, matrixPatterns[index][row]);
  }
}

// =====================================================
// SD / MP3
// =====================================================

bool isMP3File(const String &name) {
  String s = name;
  s.toLowerCase();
  return s.endsWith(".mp3");
}

void scanMP3Files() {
  fileCount = 0;

  File root = SD.open("/");
  if (!root) return;

  File entry = root.openNextFile();

  while (entry && fileCount < MAX_FILES) {
    if (!entry.isDirectory()) {
      String name = entry.name();

      if (isMP3File(name)) {
        if (!name.startsWith("/")) {
          name = "/" + name;
        }

        mp3Files[fileCount++] = name;
      }
    }

    entry.close();
    entry = root.openNextFile();
  }

  root.close();
}

void stopAudio() {
  if (mp3) {
    if (mp3->isRunning()) {
      mp3->stop();
    }

    delete mp3;
    mp3 = nullptr;
  }

  if (file) {
    delete file;
    file = nullptr;
  }
}

bool playFile(int index) {
  if (index < 0 || index >= fileCount) {
    return false;
  }

  stopAudio();

  file = new AudioFileSourceSD(mp3Files[index].c_str());

  if (!file) {
    return false;
  }

  mp3 = new AudioGeneratorMP3();

  if (!mp3) {
    delete file;
    file = nullptr;
    return false;
  }

  if (!mp3->begin(file, out)) {
    stopAudio();
    return false;
  }

  return true;
}

void nextSong() {
  if (fileCount <= 0) return;

  currentFile++;

  if (currentFile >= fileCount) {
    currentFile = 0;
  }

  songPending = true;
  songSwitchTime = millis() + 150;
}

void updateAudio() {
  if (songPending) {
    if ((long)(millis() - songSwitchTime) >= 0) {
      if (playFile(currentFile)) {
        songPending = false;
      } else {
        songSwitchTime = millis() + 500;
      }
    }

    return;
  }

  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      nextSong();
    }
  }
}

// =====================================================
// RING HELPERS
// =====================================================

uint32_t wheelColor(byte pos) {
  pos = 255 - pos;

  if (pos < 85) {
    return strip.Color(255 - pos * 3, 0, pos * 3);
  }

  if (pos < 170) {
    pos -= 85;
    return strip.Color(0, pos * 3, 255 - pos * 3);
  }

  pos -= 170;
  return strip.Color(pos * 3, 255 - pos * 3, 0);
}

uint32_t scaleColor(uint32_t c, byte b) {
  byte r = (c >> 16) & 0xFF;
  byte g = (c >> 8) & 0xFF;
  byte bl = c & 0xFF;

  r = ((uint16_t)r * b) / 255;
  g = ((uint16_t)g * b) / 255;
  bl = ((uint16_t)bl * b) / 255;

  return strip.Color(r, g, bl);
}

void fillColor(uint32_t c) {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, c);
  }
}

void showRingSolid(uint32_t c) {
  fillColor(c);
  strip.show();
}

// =====================================================
// 60 LED RING EFFECTS
// =====================================================

void updateRingEffect(int effect) {
  unsigned long now = millis();

  if (now - lastRingUpdate < 30) {
    return;
  }

  lastRingUpdate = now;
  ringStep++;

  int s = ringStep;
  byte hue = (ringStep * 5) & 0xFF;

  strip.clear();

  switch (effect) {

    case 0:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((s / 3) + i) % 2 == 0)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
      }
      break;

    case 1:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((s / 3) + i) % 3 == 0)
          strip.setPixelColor(i, strip.Color(0, 255, 0));
      }
      break;

    case 2:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((s / 3) + i) % 4 == 0)
          strip.setPixelColor(i, strip.Color(0, 100, 255));
      }
      break;

    case 3:
      fillColor((s % 12 < 6) ? strip.Color(255, 0, 80) : 0);
      break;

    case 4:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (i + s) % NUM_LEDS;
        strip.setPixelColor(p, wheelColor((i * 16 + s * 5) & 255));
      }
      break;

    case 5:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 5 == 0)
          strip.setPixelColor(i, strip.Color(255, 255, 255));
      }
      break;

    case 6:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (random(8) == 0)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
      }
      break;

    case 7:
      for (int i = 0; i < NUM_LEDS; i++) {
        byte b = (sin((i * 30 + s * 8) * 0.0174533) + 1.0) * 127;
        strip.setPixelColor(i, scaleColor(strip.Color(0, 80, 255), b));
      }
      break;

    case 8:
      for (int i = 0; i < NUM_LEDS / 2; i++) {
        int a = (i + s) % NUM_LEDS;
        int b = (NUM_LEDS - 1 - i + s) % NUM_LEDS;
        strip.setPixelColor(a, strip.Color(255, 80, 0));
        strip.setPixelColor(b, strip.Color(255, 80, 0));
      }
      break;

    case 9:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (i == s % NUM_LEDS || i == (NUM_LEDS - 1 - s % NUM_LEDS))
          strip.setPixelColor(i, strip.Color(255, 255, 0));
      }
      break;

    case 10:
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, wheelColor((i * 16 + hue) & 255));
      }
      break;

    case 11:
      for (int i = 0; i < NUM_LEDS; i++) {
        int d = abs(i - (s % NUM_LEDS));
        if (d < 4)
          strip.setPixelColor(i, scaleColor(strip.Color(255, 0, 120), 255 - d * 50));
      }
      break;

    case 12:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s + i * 2) % NUM_LEDS;
        strip.setPixelColor(p, strip.Color(0, 255, 255));
      }
      break;

    case 13:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 7 < 2)
          strip.setPixelColor(i, strip.Color(180, 0, 255));
      }
      break;

    case 14:
      for (int i = 0; i < NUM_LEDS; i++) {
        int x = (i + s) % NUM_LEDS;
        if (x == 0 || x == 1 || x == 2)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
      }
      break;

    case 15:
      if ((s % 14) < 4)
        showRingSolid(strip.Color(255, 255, 255));
      else
        strip.clear();
      return;

    case 16:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 8 < 3)
          strip.setPixelColor(i, wheelColor((i * 20 + hue) & 255));
      }
      break;

    case 17:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((i / 2) + s) % 3 == 0)
          strip.setPixelColor(i, strip.Color(255, 0, 255));
      }
      break;

    case 18:
      for (int i = 0; i < NUM_LEDS; i++) {
        byte b = ((s + i * 16) & 255);
        strip.setPixelColor(i, scaleColor(strip.Color(255, 40, 0), b));
      }
      break;

    case 19:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 6 == 0)
          strip.setPixelColor(i, strip.Color(0, 255, 0));
        if ((i + s + 3) % 6 == 0)
          strip.setPixelColor(i, strip.Color(0, 0, 255));
      }
      break;

    case 20:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s / 2 + i) % NUM_LEDS;
        if (p < 8)
          strip.setPixelColor(i, wheelColor((i * 25 + hue) & 255));
      }
      break;

    case 21:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((s + i * 3) % NUM_LEDS < 2)
          strip.setPixelColor(i, strip.Color(255, 20, 20));
      }
      break;

    case 22:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((s + i * 5) % NUM_LEDS < 3)
          strip.setPixelColor(i, strip.Color(20, 120, 255));
      }
      break;

    case 23:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((s + i * 7) % NUM_LEDS < 4)
          strip.setPixelColor(i, strip.Color(255, 0, 255));
      }
      break;

    case 24:
      for (int i = 0; i < NUM_LEDS; i++) {
        int q = (i + s) % NUM_LEDS;
        if (q == 0 || q == 8)
          strip.setPixelColor(i, strip.Color(255, 180, 0));
      }
      break;

    case 25:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 4 == 0)
          strip.setPixelColor(i, strip.Color(255, 60, 0));
        if ((i + s + 2) % 4 == 0)
          strip.setPixelColor(i, strip.Color(0, 120, 255));
      }
      break;

    case 26:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((i + s) % 10) < 5)
          strip.setPixelColor(i, strip.Color(0, 255, 100));
      }
      break;

    case 27:
      for (int i = 0; i < NUM_LEDS; i++) {
        byte b = ((sin((i * 45 + s * 10) * 0.0174533) + 1.0) * 127);
        strip.setPixelColor(i, scaleColor(strip.Color(255, 0, 80), b));
      }
      break;

    case 28:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 3 != 0)
          strip.setPixelColor(i, wheelColor((hue + i * 12) & 255));
      }
      break;

    case 29:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 9 == 0)
          strip.setPixelColor(i, strip.Color(255, 255, 255));
        else if ((i + s) % 9 == 1)
          strip.setPixelColor(i, strip.Color(120, 0, 255));
      }
      break;

    case 30:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s + i * 2) % NUM_LEDS;
        strip.setPixelColor(p, wheelColor((s * 10 + i * 20) & 255));
      }
      break;

    case 31:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 2 == 0)
          strip.setPixelColor(i, strip.Color(255, 0, 255));
        else
          strip.setPixelColor(i, strip.Color(0, 80, 255));
      }
      break;

    case 32:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 5 < 2)
          strip.setPixelColor(i, strip.Color(255, 120, 0));
      }
      break;

    case 33:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i * 2 + s) % 7 == 0)
          strip.setPixelColor(i, strip.Color(0, 255, 255));
      }
      break;

    case 34:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i * 3 + s) % 8 < 2)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
      }
      break;

    case 35:
      for (int i = 0; i < NUM_LEDS; i++) {
        int pos = (s / 3) % NUM_LEDS;
        if (i == pos)
          strip.setPixelColor(i, strip.Color(255, 255, 255));
        else if (abs(i - pos) == 1)
          strip.setPixelColor(i, strip.Color(100, 100, 255));
      }
      break;

    case 36:
      for (int i = 0; i < NUM_LEDS; i++) {
        int pos = (s / 3) % NUM_LEDS;
        int d = abs(i - pos);
        if (d < 5)
          strip.setPixelColor(i, wheelColor((hue + d * 25) & 255));
      }
      break;

    case 37:
      for (int i = 0; i < NUM_LEDS; i++) {
        int pos = (NUM_LEDS - 1 - s / 2) % NUM_LEDS;
        if (i == pos)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
        if (i == (pos + 8) % NUM_LEDS)
          strip.setPixelColor(i, strip.Color(0, 0, 255));
      }
      break;

    case 38:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((i + s) % 16) < 8)
          strip.setPixelColor(i, strip.Color(120, 0, 255));
      }
      break;

    case 39:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 8 < 4)
          strip.setPixelColor(i, wheelColor((hue + i * 30) & 255));
      }
      break;

    case 40:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 12 == 0 || (i + s) % 12 == 1)
          strip.setPixelColor(i, strip.Color(255, 255, 0));
      }
      break;

    case 41:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 11 < 3)
          strip.setPixelColor(i, strip.Color(0, 255, 0));
      }
      break;

    case 42:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 13 < 4)
          strip.setPixelColor(i, strip.Color(0, 150, 255));
      }
      break;

    case 43:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i * 4 + s) % 15 < 5)
          strip.setPixelColor(i, strip.Color(255, 0, 120));
      }
      break;

    case 44:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i * 5 + s) % 17 < 6)
          strip.setPixelColor(i, wheelColor((i * 20 + hue) & 255));
      }
      break;

    case 45:
      if ((s % 20) < 2)
        fillColor(strip.Color(255, 255, 255));
      else if ((s % 20) < 5)
        fillColor(strip.Color(255, 0, 0));
      break;

    case 46:
      if ((s % 18) < 3)
        fillColor(strip.Color(0, 0, 255));
      else if ((s % 18) < 6)
        fillColor(strip.Color(180, 0, 255));
      break;

    case 47:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s + i * 4) % NUM_LEDS;
        if (p < 4)
          strip.setPixelColor(i, strip.Color(255, 80, 0));
      }
      break;

    case 48:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s * 2 + i * 5) % NUM_LEDS;
        if (p < 3)
          strip.setPixelColor(i, strip.Color(0, 255, 180));
      }
      break;

    case 49:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (((i + s) % 6) == 0)
          strip.setPixelColor(i, strip.Color(255, 255, 255));
        if (((i + s) % 6) == 1)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
        if (((i + s) % 6) == 2)
          strip.setPixelColor(i, strip.Color(0, 0, 255));
      }
      break;

    case 50:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s / 2) % NUM_LEDS;
        int d = min(abs(i - p), NUM_LEDS - abs(i - p));

        if (d < 2)
          strip.setPixelColor(i, strip.Color(255, 255, 255));
        else if (d < 5)
          strip.setPixelColor(i, strip.Color(255, 80, 0));
      }
      break;

    case 51:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s / 2) % NUM_LEDS;
        int d = min(abs(i - p), NUM_LEDS - abs(i - p));

        if (d < 2)
          strip.setPixelColor(i, strip.Color(255, 0, 120));
        else if (d < 5)
          strip.setPixelColor(i, strip.Color(50, 0, 255));
      }
      break;

    case 52:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 10 < 2)
          strip.setPixelColor(i, wheelColor((hue + i * 10) & 255));
      }
      break;

    case 53:
      for (int i = 0; i < NUM_LEDS; i++) {
        int phase = (i * 60 + s * 12) & 255;
        byte b = phase < 128 ? phase * 2 : (255 - phase) * 2;
        strip.setPixelColor(i, scaleColor(wheelColor((i * 15 + hue) & 255), b));
      }
      break;

    case 54:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i % 4) == ((s / 2) % 4))
          strip.setPixelColor(i, strip.Color(255, 0, 0));
        else if ((i % 4) == (((s / 2) + 2) % 4))
          strip.setPixelColor(i, strip.Color(0, 0, 255));
      }
      break;

    case 55:
      for (int i = 0; i < NUM_LEDS; i++) {
        if ((i + s) % 4 == 0)
          strip.setPixelColor(i, strip.Color(255, 255, 255));

        if ((i + s) % 4 == 1)
          strip.setPixelColor(i, strip.Color(255, 0, 150));
      }
      break;

    case 56:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s / 2 + i) % NUM_LEDS;

        if (p < 6)
          strip.setPixelColor(i, wheelColor((hue + i * 15) & 255));
      }
      break;

    case 57:
      for (int i = 0; i < NUM_LEDS; i++) {
        int p = (s / 2 + i) % NUM_LEDS;

        if (p == 0 || p == 1 || p == 15)
          strip.setPixelColor(i, strip.Color(255, 255, 255));

        if (p == 7 || p == 8)
          strip.setPixelColor(i, strip.Color(255, 0, 0));
      }
      break;

    case 58:
      for (int i = 0; i < NUM_LEDS; i++) {
        if (random(10) < 2)
          strip.setPixelColor(i, wheelColor(random(256)));
      }
      break;

    case 59:
      for (int i = 0; i < NUM_LEDS; i++) {
        byte b = ((sin((i * 40 + s * 12) * 0.0174533) + 1.0) * 127);

        if ((s / 4 + i) % 2 == 0)
          strip.setPixelColor(i, scaleColor(strip.Color(255, 0, 80), b));
        else
          strip.setPixelColor(i, scaleColor(strip.Color(0, 120, 255), b));
      }
      break;
  }

  strip.show();
}

// =====================================================
// TOUCH GPIO34
// =====================================================

void checkTouch() {
  bool raw = digitalRead(TOUCH_PIN);

  if (raw != lastTouchState) {
    touchDebounceTime = millis();
    lastTouchState = raw;
  }

  if (millis() - touchDebounceTime > 120) {
    if (raw != stableTouchState) {
      stableTouchState = raw;

      if (stableTouchState) {
        currentMatrixPattern++;

        if (currentMatrixPattern >= MATRIX_EFFECT_COUNT) {
          currentMatrixPattern = 0;
        }

        displayMatrixPattern(currentMatrixPattern);
      }
    }
  }
}

// =====================================================
// RING CHANGE
// =====================================================

void updateRingSelector() {
  unsigned long now = millis();

  if (now - lastRingChange >= RING_INTERVAL) {
    lastRingChange = now;

    currentRingEffect++;

    if (currentRingEffect >= RING_EFFECT_COUNT) {
      currentRingEffect = 0;
    }

    ringStep = 0;
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, SPI)) {
    while (true) {
      delay(1000);
    }
  }

  scanMP3Files();

  if (fileCount == 0) {
    while (true) {
      delay(1000);
    }
  }

  out = new FixedRateAudioOutput();

  if (!out) {
    while (true) {
      delay(1000);
    }
  }

  out->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  out->SetGain(1.0);
  out->SetOutputModeMono(true);

  if (!out->begin()) {
    while (true) {
      delay(1000);
    }
  }

  strip.begin();
  strip.setBrightness(50);
  strip.clear();
  strip.show();

  initMatrix();

  pinMode(TOUCH_PIN, INPUT);

  currentMatrixPattern = 0;
  displayMatrixPattern(currentMatrixPattern);

  currentRingEffect = 0;
  ringStep = 0;

  currentFile = 0;
  playFile(currentFile);

  lastRingChange = millis();
  lastRingUpdate = millis();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  updateAudio();

  updateRingSelector();

  updateRingEffect(currentRingEffect);

  checkTouch();
}