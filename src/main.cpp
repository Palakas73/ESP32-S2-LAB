#include <Arduino.h>
#include <FS.h>  // Keeps PlatformIO's framework-library resolver aware of USB's transitive FS dependency.
#include <USB.h>
#include <USBHIDMouse.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>

#include "jiggler_engine.h"

class NonBlockingMouse : public USBHIDMouse {
 public:
  bool moveRelative(int8_t x, int8_t y) {
    const hid_mouse_report_t report = {.buttons = 0, .x = x, .y = y, .wheel = 0, .pan = 0};
    return hid.SendReport(HID_REPORT_ID_MOUSE, &report, sizeof(report), 0);
  }
};

namespace {
constexpr uint8_t kMaxDeltaPerReport = 127;
constexpr size_t kCommandCapacity = 96;
NonBlockingMouse mouse;
JigglerEngine engine([](uint32_t upper, void *) { return upper ? esp_random() % upper : 0; });
char commandBuffer[kCommandCapacity];
size_t commandLength = 0;
bool commandOverflow = false;
int32_t pendingX = 0;
int32_t pendingY = 0;
int32_t submittedX = 960;
int32_t submittedY = 540;
uint32_t lastHeartbeat = 0;

const char *modeName(JigglerMode mode) {
  switch (mode) {
    case JigglerMode::Medium: return "MEDIUM";
    case JigglerMode::Fast: return "FAST";
    case JigglerMode::Slow:
    default: return "SLOW";
  }
}

bool parseMode(const char *name, JigglerMode &mode) {
  if (strcmp(name, "SLOW") == 0) mode = JigglerMode::Slow;
  else if (strcmp(name, "MEDIUM") == 0) mode = JigglerMode::Medium;
  else if (strcmp(name, "FAST") == 0) mode = JigglerMode::Fast;
  else return false;
  return true;
}

String deviceInfo() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flashBytes = 0;
  esp_flash_get_size(nullptr, &flashBytes);
  String out;
  out.reserve(320);
  out += "model="; out += ESP.getChipModel(); out += "\nrevision="; out += String(ESP.getChipRevision());
  out += "\ncores="; out += String(chip.cores); out += "\nfrequency_mhz="; out += String(ESP.getCpuFreqMHz());
  out += "\nflash_bytes="; out += String(flashBytes); out += "\nheap_bytes="; out += String(ESP.getHeapSize());
  out += "\nfree_heap_bytes="; out += String(ESP.getFreeHeap()); out += "\npsram_bytes="; out += String(ESP.getPsramSize());
  out += "\nfree_psram_bytes="; out += String(ESP.getFreePsram()); out += "\nuptime_ms="; out += String(millis());
  out += "\nusb=CDC+HID-Mouse\n";
  return out;
}

void printStatus() {
  const JigglerProfile p = engine.profile();
  Serial.printf("JIGGLE STATUS active=%u mode=%s screen_width=%u screen_height=%u virtual_x=%ld virtual_y=%ld "
                "path=%s trajectory_ms=%lu pause_min_ms=%lu pause_max_ms=%lu\n",
                engine.active() ? 1 : 0, modeName(engine.mode()), engine.screenWidth(), engine.screenHeight(),
                static_cast<long>(engine.virtualX()), static_cast<long>(engine.virtualY()), engine.moving() ? "MOVING" : "IDLE",
                static_cast<unsigned long>(p.trajectoryMs), static_cast<unsigned long>(p.pauseMinMs),
                static_cast<unsigned long>(p.pauseMaxMs));
}

void clearPendingAndStop() {
  pendingX = 0;
  pendingY = 0;
  engine.stopAt(submittedX, submittedY);
}

void handleJiggle(const char *args) {
  char action[16] = {};
  if (sscanf(args, "%15s", action) != 1) {
    Serial.println("ERROR usage: JIGGLE ON|OFF|SLOW|MEDIUM|FAST|STATUS|SCREEN width height");
    return;
  }
  for (char *p = action; *p; ++p) *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));

  if (strcmp(action, "ON") == 0) {
    engine.setActive(true, millis());
    Serial.printf("OK active=1 mode=%s\n", modeName(engine.mode()));
  } else if (strcmp(action, "OFF") == 0) {
    clearPendingAndStop();
    Serial.println("OK active=0");
  } else if (strcmp(action, "STATUS") == 0) {
    printStatus();
  } else if (strcmp(action, "SCREEN") == 0) {
    unsigned long width = 0, height = 0;
    char extra = 0;
    if (sscanf(args, "%*s %lu %lu %c", &width, &height, &extra) != 2 || width > UINT16_MAX || height > UINT16_MAX ||
        !engine.setScreen(static_cast<uint16_t>(width), static_cast<uint16_t>(height), millis())) {
      Serial.println("ERROR screen dimensions must be width 320..32767 and height 240..32767");
      return;
    }
    submittedX = engine.virtualX();
    submittedY = engine.virtualY();
    pendingX = pendingY = 0;
    Serial.printf("OK screen_width=%u screen_height=%u\n", engine.screenWidth(), engine.screenHeight());
  } else {
    JigglerMode mode;
    if (!parseMode(action, mode)) {
      Serial.println("ERROR unknown JIGGLE command");
      return;
    }
    engine.setMode(mode, millis());
    const JigglerProfile p = engine.profile();
    Serial.printf("OK mode=%s active=%u trajectory_ms=%lu pause_min_ms=%lu pause_max_ms=%lu\n", modeName(mode),
                  engine.active() ? 1 : 0, static_cast<unsigned long>(p.trajectoryMs),
                  static_cast<unsigned long>(p.pauseMinMs), static_cast<unsigned long>(p.pauseMaxMs));
  }
}

void handleCommand(char *line) {
  while (*line && isspace(static_cast<unsigned char>(*line))) ++line;
  for (char *p = line; *p; ++p) *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  if (strcmp(line, "PING") == 0) Serial.println("PONG");
  else if (strcmp(line, "INFO") == 0) { Serial.print(deviceInfo()); Serial.println("END_INFO"); }
  else if (strcmp(line, "REBOOT") == 0) { Serial.println("REBOOTING"); Serial.flush(); delay(100); ESP.restart(); }
  else if (strncmp(line, "JIGGLE", 6) == 0 && (line[6] == '\0' || isspace(static_cast<unsigned char>(line[6])))) {
    handleJiggle(line + 6);
  } else if (*line) {
    Serial.println("ERROR unknown command");
  }
}

void pollSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (commandOverflow) Serial.println("ERROR command too long");
      else {
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
      }
      commandLength = 0;
      commandOverflow = false;
    } else if (commandLength + 1 < kCommandCapacity) {
      if (!commandOverflow) commandBuffer[commandLength++] = c;
    } else {
      commandOverflow = true;
    }
  }
}

void updateJiggler() {
  int32_t dx = 0, dy = 0;
  if (engine.update(millis(), dx, dy)) {
    pendingX += dx;
    pendingY += dy;
  }
  if (pendingX == 0 && pendingY == 0) return;
  const int8_t reportX = static_cast<int8_t>(constrain(pendingX, -kMaxDeltaPerReport, kMaxDeltaPerReport));
  const int8_t reportY = static_cast<int8_t>(constrain(pendingY, -kMaxDeltaPerReport, kMaxDeltaPerReport));
  if (mouse.moveRelative(reportX, reportY)) {
    pendingX -= reportX;
    pendingY -= reportY;
    submittedX = constrain(submittedX + reportX, 0, static_cast<int32_t>(engine.screenWidth()) - 1);
    submittedY = constrain(submittedY + reportY, 0, static_cast<int32_t>(engine.screenHeight()) - 1);
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  USB.manufacturerName("WEMOS");
  USB.productName("ESP32-S2 Mini CDC + HID Mouse");
  mouse.begin();
  USB.begin();
  const uint32_t waitStarted = millis();
  while (!Serial && millis() - waitStarted < 3000) delay(10);
  Serial.println();
  Serial.println("ESP32-S2 Mini CDC + HID Mouse ready; Jiggler disabled");
  Serial.print(deviceInfo());
  Serial.println("Commands: PING, INFO, REBOOT, JIGGLE ON|OFF|SLOW|MEDIUM|FAST|STATUS|SCREEN w h");
  lastHeartbeat = millis();
}

void loop() {
  pollSerial();
  updateJiggler();
  const uint32_t now = millis();
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    Serial.printf("HEARTBEAT uptime_ms=%lu free_heap=%u active=%u mode=%s\n", static_cast<unsigned long>(now),
                  static_cast<unsigned>(ESP.getFreeHeap()), engine.active() ? 1 : 0, modeName(engine.mode()));
  }
  delay(1);
}
