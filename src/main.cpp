#include <Arduino.h>
#include <FS.h>  // Keeps PlatformIO's framework-library resolver aware of USB's transitive FS dependency.
#include <Preferences.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>

#include "jiggler_engine.h"
#include "button_gestures.h"
#include "status_led.h"

class NonBlockingMouse : public USBHIDMouse {
 public:
  bool moveRelative(int8_t x, int8_t y) {
    // SendReport(..., 0) can queue a report and then return false because
    // completion has not arrived yet. Retrying that result duplicates motion.
    // TinyUSB returns true as soon as this report is accepted by the endpoint.
    return hid.ready() && tud_hid_n_mouse_report(0, HID_REPORT_ID_MOUSE, 0, x, y, 0, 0);
  }
};

namespace {
constexpr uint8_t kMaxDeltaPerReport = 2;
constexpr uint8_t kReportIntervalMs = 8;
constexpr size_t kCommandCapacity = 96;
constexpr uint8_t kBootButtonPin = 0;
constexpr uint8_t kStatusLedPin = LED_BUILTIN;
constexpr uint16_t kButtonDebounceMs = 30;
constexpr uint16_t kDoubleClickWindowMs = 300;
constexpr const char *kPreferencesNamespace = "jiggler";
constexpr const char *kModePreferenceKey = "mode";
// LOLIN S2 Mini GPIO15 LED is high-active.
constexpr bool kStatusLedActiveLow = false;
NonBlockingMouse mouse;
JigglerEngine engine;
ButtonGestures bootButton(kButtonDebounceMs, kDoubleClickWindowMs);
Preferences preferences;
bool preferencesReady = false;
bool statusLedOn = false;
StatusLedIndicator statusLed;
bool ledSelfTestPassed = false;
uint32_t buttonPresses = 0;
bool buttonSelfTestPassed = false;
char commandBuffer[kCommandCapacity];
size_t commandLength = 0;
bool commandOverflow = false;
int32_t pendingX = 0;
int32_t pendingY = 0;
int32_t submittedX = 1059;
int32_t submittedY = 539;
uint32_t nextReportAt = 0;
uint32_t txReports = 0;
uint8_t txMaxAbsX = 0;
uint8_t txMaxAbsY = 0;
int32_t txSumX = 0;
int32_t txSumY = 0;
uint32_t lastHeartbeat = 0;

void writeStatusLed(bool on);

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
                "path=%s trajectory_ms=%lu pause_min_ms=%lu pause_max_ms=%lu motion=circle circle_radius_px=%ld "
                "circle_center_x=%ld circle_center_y=%ld hid_step_limit=%u hid_interval_ms=%u "
                "submitted_x=%ld submitted_y=%ld pending_x=%ld pending_y=%ld "
                "tx_reports=%lu tx_sum_x=%ld tx_sum_y=%ld button_gpio=%u led_gpio=%u gesture_test=%s "
                "led_on=%u led_pin_level=%u led_active_low=%u led_window_remaining_ms=%lu led_test=%s button_presses=%lu "
                "tx_max_abs_x=%u tx_max_abs_y=%u\n",
                engine.active() ? 1 : 0, modeName(engine.mode()), engine.screenWidth(), engine.screenHeight(),
                static_cast<long>(engine.virtualX()), static_cast<long>(engine.virtualY()), engine.moving() ? "MOVING" : "IDLE",
                static_cast<unsigned long>(p.trajectoryMs), static_cast<unsigned long>(p.pauseMinMs),
                static_cast<unsigned long>(p.pauseMaxMs), static_cast<long>(JigglerEngine::circleRadiusPx()),
                static_cast<long>((static_cast<int32_t>(engine.screenWidth()) - 1) / 2),
                static_cast<long>((static_cast<int32_t>(engine.screenHeight()) - 1) / 2),
                kMaxDeltaPerReport, kReportIntervalMs,
                static_cast<long>(submittedX), static_cast<long>(submittedY),
                static_cast<long>(pendingX), static_cast<long>(pendingY),
                static_cast<unsigned long>(txReports), static_cast<long>(txSumX), static_cast<long>(txSumY),
                kBootButtonPin, kStatusLedPin,
                buttonSelfTestPassed ? "PASS" : "FAIL", statusLedOn ? 1 : 0, digitalRead(kStatusLedPin),
                kStatusLedActiveLow ? 1 : 0, static_cast<unsigned long>(statusLed.remainingMs(millis())),
                ledSelfTestPassed ? "PASS" : "FAIL", static_cast<unsigned long>(buttonPresses), txMaxAbsX, txMaxAbsY);
}

void setJigglerMode(JigglerMode mode, uint32_t nowMs) {
  if (engine.mode() == mode) return;
  engine.setMode(mode, nowMs);
  if (engine.active()) {
    statusLed.wake(nowMs);
    writeStatusLed(true);
  }
  if (preferencesReady && preferences.putUChar(kModePreferenceKey, static_cast<uint8_t>(mode)) == 0) {
    Serial.println("ERROR mode persistence failed");
  }
}

void clearPendingAndStop() {
  pendingX = 0;
  pendingY = 0;
  engine.stopAt(submittedX, submittedY);
  statusLed.stop();
  writeStatusLed(false);
}

void activateJiggler(uint32_t nowMs) {
  if (engine.active()) return;
  engine.setActive(true, nowMs);
  statusLed.wake(nowMs);
  writeStatusLed(true);
  pendingX = pendingY = 0;
  submittedX = engine.virtualX();
  submittedY = engine.virtualY();
  txReports = 0;
  txMaxAbsX = txMaxAbsY = 0;
  txSumX = txSumY = 0;
}

void handleJiggle(const char *args) {
  char action[16] = {};
  if (sscanf(args, "%15s", action) != 1) {
    Serial.println("ERROR usage: JIGGLE ON|OFF|SLOW|MEDIUM|FAST|STATUS|SCREEN width height");
    return;
  }
  for (char *p = action; *p; ++p) *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));

  if (strcmp(action, "ON") == 0) {
    activateJiggler(millis());
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
    setJigglerMode(mode, millis());
    const JigglerProfile p = engine.profile();
    Serial.printf("OK mode=%s active=%u trajectory_ms=%lu pause_min_ms=%lu pause_max_ms=%lu\n", modeName(mode),
                  engine.active() ? 1 : 0, static_cast<unsigned long>(p.trajectoryMs),
                  static_cast<unsigned long>(p.pauseMinMs), static_cast<unsigned long>(p.pauseMaxMs));
  }
}

void handleBootGesture(ButtonGestureEvent event, uint32_t nowMs) {
  if (event == ButtonGestureEvent::ShortPress) {
    if (engine.active()) clearPendingAndStop();
    else activateJiggler(nowMs);
    Serial.printf("BOOT SHORT active=%u mode=%s\n", engine.active() ? 1 : 0, modeName(engine.mode()));
  } else if (event == ButtonGestureEvent::DoublePress) {
    setJigglerMode(nextJigglerMode(engine.mode()), nowMs);
    Serial.printf("BOOT DOUBLE active=%u mode=%s\n", engine.active() ? 1 : 0, modeName(engine.mode()));
  }
}

void updateBootButton(uint32_t nowMs) {
  const bool pressed = digitalRead(kBootButtonPin) == LOW;
  const bool wasPressed = bootButton.pressed();
  const ButtonGestureEvent event = bootButton.update(pressed, nowMs);
  if (!wasPressed && bootButton.pressed()) {
    ++buttonPresses;
    Serial.printf("BOOT PRESS count=%lu\n", static_cast<unsigned long>(buttonPresses));
  }
  handleBootGesture(event, nowMs);
}

void writeStatusLed(bool on) {
  if (statusLedOn == on) return;
  statusLedOn = on;
  digitalWrite(kStatusLedPin, on == kStatusLedActiveLow ? LOW : HIGH);
}

void updateStatusLed(uint32_t nowMs) {
  writeStatusLed(statusLed.on(nowMs, engine.mode(), engine.active()));
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
  const uint32_t nowMs = millis();
  if (engine.update(nowMs, dx, dy)) {
    pendingX += dx;
    pendingY += dy;
  }
  if (pendingX == 0 && pendingY == 0) return;
  if (static_cast<int32_t>(nowMs - nextReportAt) < 0) return;
  const int8_t reportX = static_cast<int8_t>(constrain(pendingX, -kMaxDeltaPerReport, kMaxDeltaPerReport));
  const int8_t reportY = static_cast<int8_t>(constrain(pendingY, -kMaxDeltaPerReport, kMaxDeltaPerReport));
  if (mouse.moveRelative(reportX, reportY)) {
    nextReportAt = nowMs + kReportIntervalMs;
    ++txReports;
    const uint8_t magnitudeX = reportX < 0 ? -reportX : reportX;
    const uint8_t magnitudeY = reportY < 0 ? -reportY : reportY;
    if (magnitudeX > txMaxAbsX) txMaxAbsX = magnitudeX;
    if (magnitudeY > txMaxAbsY) txMaxAbsY = magnitudeY;
    txSumX += reportX;
    txSumY += reportY;
    pendingX -= reportX;
    pendingY -= reportY;
    submittedX = constrain(submittedX + reportX, 0, static_cast<int32_t>(engine.screenWidth()) - 1);
    submittedY = constrain(submittedY + reportY, 0, static_cast<int32_t>(engine.screenHeight()) - 1);
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kBootButtonPin, INPUT_PULLUP);
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, kStatusLedActiveLow ? HIGH : LOW);
  writeStatusLed(false);
  if (preferences.begin(kPreferencesNamespace, false)) {
    preferencesReady = true;
    const uint8_t savedMode = preferences.getUChar(kModePreferenceKey, static_cast<uint8_t>(JigglerMode::Medium));
    const JigglerMode mode = savedMode <= static_cast<uint8_t>(JigglerMode::Fast)
                                 ? static_cast<JigglerMode>(savedMode)
                                 : JigglerMode::Medium;
    engine.setMode(mode, millis());
  }
  USB.manufacturerName("WEMOS");
  USB.productName("ESP32-S2 Generic Mouse");
  mouse.begin();
  USB.begin();
  const uint32_t waitStarted = millis();
  while (!Serial && millis() - waitStarted < 3000) delay(10);
  Serial.println();
  Serial.println("ESP32-S2 Mini CDC + HID Mouse ready; Jiggler disabled");
  Serial.print(deviceInfo());
  Serial.println("Commands: PING, INFO, REBOOT, JIGGLE ON|OFF|SLOW|MEDIUM|FAST|STATUS|SCREEN w h");
  buttonSelfTestPassed = runButtonGestureSelfTest();
  ledSelfTestPassed = runStatusLedSelfTest();
  Serial.printf("BOOT button GPIO%u; LED GPIO%u; gesture self-test=%s\n", kBootButtonPin, kStatusLedPin,
                buttonSelfTestPassed ? "PASS" : "FAIL");
  lastHeartbeat = millis();
}

void loop() {
  pollSerial();
  const uint32_t now = millis();
  updateBootButton(now);
  updateJiggler();
  updateStatusLed(now);
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    Serial.printf("HEARTBEAT uptime_ms=%lu free_heap=%u active=%u mode=%s\n", static_cast<unsigned long>(now),
                  static_cast<unsigned>(ESP.getFreeHeap()), engine.active() ? 1 : 0, modeName(engine.mode()));
  }
  delay(1);
}
