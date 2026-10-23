#pragma once

#include <stdint.h>

enum class JigglerMode : uint8_t { Slow, Medium, Fast };

struct JigglerProfile {
  uint32_t trajectoryMs;
  uint32_t pauseMinMs;
  uint32_t pauseMaxMs;
};

class JigglerEngine {
 public:
  JigglerEngine();

  bool setMode(JigglerMode mode, uint32_t nowMs);
  bool setScreen(uint16_t width, uint16_t height, uint32_t nowMs);
  void setActive(bool active, uint32_t nowMs);
  void stopAt(int32_t x, int32_t y);
  bool update(uint32_t nowMs, int32_t &deltaX, int32_t &deltaY);

  bool active() const { return active_; }
  bool moving() const { return moving_; }
  JigglerMode mode() const { return mode_; }
  JigglerProfile profile() const;
  uint16_t screenWidth() const { return screenWidth_; }
  uint16_t screenHeight() const { return screenHeight_; }
  static int32_t circleRadiusPx() { return 100; }
  int32_t virtualX() const { return virtualX_; }
  int32_t virtualY() const { return virtualY_; }

 private:
  void startPath(uint32_t nowMs);
  static int32_t clampPosition(float value, uint16_t limit);

  JigglerMode mode_;
  bool active_;
  bool moving_;
  uint16_t screenWidth_;
  uint16_t screenHeight_;
  int32_t virtualX_;
  int32_t virtualY_;
  int32_t lastTargetX_;
  int32_t lastTargetY_;
  float circleCenterX_;
  float circleCenterY_;
  float pathStartAngle_;
  float pathStartRadius_;
  uint32_t pathStartedAt_;
  uint32_t nextTickAt_;
};
