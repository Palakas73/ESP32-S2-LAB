#include "button_gestures.h"

#include <math.h>

ButtonGestureEvent ButtonGestures::update(bool rawPressed, uint32_t nowMs) {
  ButtonGestureEvent event = ButtonGestureEvent::None;
  if (rawPressed != rawPressed_) {
    rawPressed_ = rawPressed;
    rawChangedAt_ = nowMs;
  }

  if (rawPressed_ != stablePressed_ && elapsed(nowMs, rawChangedAt_, debounceMs_)) {
    stablePressed_ = rawPressed_;
    if (stablePressed_) {
      if (clickPending_) {
        const uint32_t pressGap = rawChangedAt_ - firstReleaseAt_;
        if (pressGap <= doubleClickWindowMs_) {
          clickPending_ = false;
          secondPressCandidate_ = true;
        } else {
          clickPending_ = false;
          event = ButtonGestureEvent::ShortPress;
        }
      }
    } else if (secondPressCandidate_) {
      secondPressCandidate_ = false;
      event = ButtonGestureEvent::DoublePress;
    } else {
      clickPending_ = true;
      firstReleaseAt_ = nowMs;
    }
  }

  if (clickPending_ && !rawPressed_ && elapsed(nowMs, firstReleaseAt_, doubleClickWindowMs_ + 1)) {
    clickPending_ = false;
    event = ButtonGestureEvent::ShortPress;
  }
  return event;
}

JigglerMode nextJigglerMode(JigglerMode mode) {
  switch (mode) {
    case JigglerMode::Slow: return JigglerMode::Medium;
    case JigglerMode::Medium: return JigglerMode::Fast;
    case JigglerMode::Fast:
    default: return JigglerMode::Slow;
  }
}

namespace {
bool press(ButtonGestures &button, uint32_t start, uint16_t debounceMs = 30) {
  if (button.update(true, start) != ButtonGestureEvent::None) return false;
  if (button.update(true, start + debounceMs - 1) != ButtonGestureEvent::None) return false;
  return button.update(true, start + debounceMs) == ButtonGestureEvent::None;
}

bool release(ButtonGestures &button, uint32_t start, uint16_t debounceMs = 30) {
  if (button.update(false, start) != ButtonGestureEvent::None) return false;
  if (button.update(false, start + debounceMs - 1) != ButtonGestureEvent::None) return false;
  return button.update(false, start + debounceMs) == ButtonGestureEvent::None;
}
}  // namespace

bool runButtonGestureSelfTest() {
  JigglerEngine defaults;
  if (defaults.active() || defaults.mode() != JigglerMode::Medium) return false;
  JigglerEngine stateTest;
  stateTest.setActive(true, 0);
  stateTest.setMode(nextJigglerMode(stateTest.mode()), 1);
  if (!stateTest.active()) return false;
  stateTest.setActive(false, 2);
  int32_t stoppedX = 0, stoppedY = 0;
  if (stateTest.update(100, stoppedX, stoppedY)) return false;
  stateTest.stopAt(400, 400);
  stateTest.setActive(true, 101);
  if (stateTest.virtualX() != 1059 || stateTest.virtualY() != 539) return false;
  stateTest.setActive(false, 102);

  JigglerEngine circleTest;
  if (!circleTest.setScreen(640, 480, 0)) return false;
  circleTest.setActive(true, 0);
  circleTest.update(0, stoppedX, stoppedY);
  circleTest.update(900, stoppedX, stoppedY);
  const float centerX = 319.0f;
  const float centerY = 239.0f;
  float fromCenterX = circleTest.virtualX() - centerX;
  float fromCenterY = circleTest.virtualY() - centerY;
  float radius = sqrtf(fromCenterX * fromCenterX + fromCenterY * fromCenterY);
  if (radius < 99.0f || radius > 101.0f) return false;
  if (circleTest.virtualX() < 0 || circleTest.virtualX() >= 640 || circleTest.virtualY() < 0 ||
      circleTest.virtualY() >= 480) return false;
  circleTest.update(6000, stoppedX, stoppedY);
  fromCenterX = circleTest.virtualX() - centerX;
  fromCenterY = circleTest.virtualY() - centerY;
  radius = sqrtf(fromCenterX * fromCenterX + fromCenterY * fromCenterY);
  if (radius < 99.0f || radius > 101.0f || !circleTest.moving()) return false;

  // Three consecutive closed turns in every mode, including each boundary.
  const JigglerMode continuousModes[] = {JigglerMode::Slow, JigglerMode::Medium, JigglerMode::Fast};
  for (JigglerMode mode : continuousModes) {
    JigglerEngine continuous;
    continuous.setMode(mode, 0);
    const JigglerProfile profile = continuous.profile();
    if (profile.pauseMinMs || profile.pauseMaxMs) return false;
    continuous.setActive(true, 0);
    int32_t sumX = 0, sumY = 0;
    for (uint32_t now = 0; now <= profile.trajectoryMs * 3; now += 8) {
      int32_t dx = 0, dy = 0;
      continuous.update(now, dx, dy);
      sumX += dx; sumY += dy;
      if (!continuous.moving() || dx < -2 || dx > 2 || dy < -2 || dy > 2) return false;
      if (now % profile.trajectoryMs == 0 && (sumX || sumY)) return false;
      // Check that the next turn advances rather than entering a pause.
      if (now > profile.trajectoryMs && now % profile.trajectoryMs == 200 && !sumX && !sumY) return false;
    }
    continuous.stopAt(continuous.virtualX(), continuous.virtualY());
    if (continuous.update(profile.trajectoryMs * 3 + 8, stoppedX, stoppedY) || continuous.moving()) return false;
  }

  ButtonGestures shortButton(30, 300);
  if (!press(shortButton, 0) || !release(shortButton, 40)) return false;
  if (shortButton.update(false, 370) != ButtonGestureEvent::None) return false;
  if (shortButton.update(false, 371) != ButtonGestureEvent::ShortPress) return false;

  ButtonGestures doubleButton(30, 300);
  if (!press(doubleButton, 1000) || !release(doubleButton, 1040)) return false;
  if (doubleButton.update(false, 1200) != ButtonGestureEvent::None) return false;
  if (!press(doubleButton, 1300)) return false;
  if (doubleButton.update(false, 1340) != ButtonGestureEvent::None) return false;
  if (doubleButton.update(false, 1369) != ButtonGestureEvent::None) return false;
  if (doubleButton.update(false, 1370) != ButtonGestureEvent::DoublePress) return false;

  ButtonGestures bounceButton(30, 300);
  if (bounceButton.update(true, 0) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(false, 10) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(true, 15) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(true, 45) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(false, 50) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(true, 60) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(false, 65) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(false, 95) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(false, 395) != ButtonGestureEvent::None) return false;
  if (bounceButton.update(false, 396) != ButtonGestureEvent::ShortPress) return false;

  if (nextJigglerMode(JigglerMode::Slow) != JigglerMode::Medium) return false;
  if (nextJigglerMode(JigglerMode::Medium) != JigglerMode::Fast) return false;
  if (nextJigglerMode(JigglerMode::Fast) != JigglerMode::Slow) return false;
  return true;
}
