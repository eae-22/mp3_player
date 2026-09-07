#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"


// =============================
// SD CARD
// =============================
#define SD_CS    4
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23


// =============================
// I2S
// =============================
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 22


// =============================
// CẤU HÌNH
// =============================
#define MAX_FILES 1000


AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;


// Danh sách file MP3
String mp3Files[MAX_FILES];

int fileCount = 0;
int currentFile = 0;


// =====================================================
// QUÉT TẤT CẢ FILE .MP3
// =====================================================
void scanMP3Files() {

  File root = SD.open("/");

  if (!root) {
    Serial.println("Khong mo duoc thu muc goc!");
    return;
  }

  fileCount = 0;

  File entry = root.openNextFile();

  while (entry && fileCount < MAX_FILES) {

    if (!entry.isDirectory()) {

      String filename = entry.name();
      String checkName = filename;

      checkName.toLowerCase();

      // Kiểm tra đuôi .mp3
      if (checkName.endsWith(".mp3")) {

        // Đảm bảo đường dẫn có /
        if (!filename.startsWith("/")) {
          filename = "/" + filename;
        }

        mp3Files[fileCount] = filename;

        Serial.print("Tim thay MP3: ");
        Serial.println(mp3Files[fileCount]);

        fileCount++;
      }
    }

    entry.close();

    entry = root.openNextFile();
  }

  root.close();


  Serial.println();

  Serial.print("Tong so file MP3: ");
  Serial.println(fileCount);
}


// =====================================================
// PHÁT FILE MP3
// =====================================================
bool playFile(int index) {

  if (index < 0 || index >= fileCount) {
    return false;
  }


  String filename = mp3Files[index];


  Serial.println();

  Serial.print("Dang phat: ");
  Serial.println(filename);


  // -----------------------------------------
  // Dừng MP3 cũ
  // -----------------------------------------

  if (mp3 != nullptr) {

    if (mp3->isRunning()) {
      mp3->stop();
    }

    delete mp3;
    mp3 = nullptr;
  }


  // -----------------------------------------
  // Đóng file cũ
  // -----------------------------------------

  if (file != nullptr) {

    delete file;
    file = nullptr;
  }


  // -----------------------------------------
  // Mở file mới
  // -----------------------------------------

  file = new AudioFileSourceSD(filename.c_str());

  if (file == nullptr) {

    Serial.println("AudioFileSourceSD FAIL");

    return false;
  }


  // -----------------------------------------
  // Tạo MP3 decoder
  // -----------------------------------------

  mp3 = new AudioGeneratorMP3();

  if (mp3 == nullptr) {

    Serial.println("AudioGeneratorMP3 FAIL");

    delete file;
    file = nullptr;

    return false;
  }


  // -----------------------------------------
  // Bắt đầu phát
  // -----------------------------------------

  if (!mp3->begin(file, out)) {

    Serial.println("MP3 begin FAIL");

    delete mp3;
    mp3 = nullptr;

    delete file;
    file = nullptr;

    return false;
  }


  Serial.println("MP3 begin OK");

  return true;
}


// =====================================================
// SETUP
// =====================================================
void setup() {

  Serial.begin(115200);

  delay(2000);

  Serial.println();
  Serial.println("=== ESP32 MP3 PLAYER ===");


  // =========================================
  // SPI
  // =========================================

  SPI.begin(
    SD_SCK,
    SD_MISO,
    SD_MOSI,
    SD_CS
  );


  // =========================================
  // SD CARD
  // =========================================

  if (!SD.begin(SD_CS, SPI)) {

    Serial.println("SD.begin FAIL");

    while (1) {
      delay(100);
    }
  }

  Serial.println("SD OK");


  // =========================================
  // QUÉT FILE MP3
  // =========================================

  scanMP3Files();


  if (fileCount == 0) {

    Serial.println("Khong tim thay file MP3!");

    while (1) {
      delay(1000);
    }
  }


  // =========================================
  // I2S
  // =========================================

  out = new AudioOutputI2S();

  out->SetPinout(
    I2S_BCLK,
    I2S_LRC,
    I2S_DOUT
  );

  out->SetGain(1.0);

  // Mono
  out->SetOutputModeMono(true);

  // Quan trọng
  out->begin();


  // =========================================
  // PHÁT FILE ĐẦU TIÊN
  // =========================================

  currentFile = 0;

  if (!playFile(currentFile)) {

    Serial.println("Khong the phat file dau tien!");

    while (1) {
      delay(1000);
    }
  }
}


// =====================================================
// LOOP
// =====================================================
void loop() {

  if (mp3 != nullptr && mp3->isRunning()) {

    // Đang phát
    if (!mp3->loop()) {

      // Bài hiện tại đã hết
      mp3->stop();

      Serial.print("Da phat xong: ");
      Serial.println(mp3Files[currentFile]);


      // =========================================
      // SANG FILE TIẾP THEO
      // =========================================

      currentFile++;


      // =========================================
      // HẾT DANH SÁCH
      // QUAY LẠI FILE ĐẦU
      // =========================================

      if (currentFile >= fileCount) {

        Serial.println();
        Serial.println("=== DA PHAT HET TAT CA ===");
        Serial.println("=== QUAY LAI TU DAU ===");

        currentFile = 0;
      }


      delay(300);


      // =========================================
      // PHÁT FILE TIẾP THEO
      // =========================================

      if (!playFile(currentFile)) {

        Serial.println("Loi phat file!");

        // Thử file tiếp theo
        currentFile++;

        if (currentFile >= fileCount) {
          currentFile = 0;
        }
      }
    }
  }
}