#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

static String deviceInfo() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flashBytes = 0;
  esp_flash_get_size(nullptr, &flashBytes);
  String out;
  out.reserve(256);
  out += "model="; out += ESP.getChipModel(); out += "\nrevision="; out += String(ESP.getChipRevision());
  out += "\ncores="; out += String(chip.cores); out += "\nfrequency_mhz="; out += String(ESP.getCpuFreqMHz());
  out += "\nflash_bytes="; out += String(flashBytes); out += "\nheap_bytes="; out += String(ESP.getHeapSize());
  out += "\nfree_heap_bytes="; out += String(ESP.getFreeHeap()); out += "\npsram_bytes="; out += String(ESP.getPsramSize());
  out += "\nfree_psram_bytes="; out += String(ESP.getFreePsram()); out += "\nuptime_ms="; out += String(millis()); out += "\n";
  return out;
}

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  Serial.println();
  Serial.println("ESP32-S2 Mini diagnostic firmware");
  Serial.println("USB CDC ready");
  Serial.print(deviceInfo());
  Serial.println("Commands: PING, INFO");
}

void loop() {
  static uint32_t lastBeat = 0;
  static String line;
  if (millis() - lastBeat >= 1000) {
    lastBeat = millis();
    Serial.printf("HEARTBEAT uptime_ms=%lu free_heap=%u\n", (unsigned long)millis(), (unsigned)ESP.getFreeHeap());
  }
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim(); line.toUpperCase();
      if (line == "PING") Serial.println("PONG");
      else if (line == "INFO") { Serial.print(deviceInfo()); Serial.println("END_INFO"); }
      else if (line == "REBOOT") { Serial.println("REBOOTING"); Serial.flush(); delay(100); ESP.restart(); }
      else if (!line.isEmpty()) Serial.println("ERROR unknown command");
      line = "";
    } else if (line.length() < 64) line += c;
  }
  delay(2);
}
