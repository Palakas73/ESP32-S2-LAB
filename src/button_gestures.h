#pragma once

#include <stdint.h>

#include "jiggler_engine.h"

enum class ButtonGestureEvent : uint8_t { None, ShortPress, DoublePress };

class ButtonGestures {
 public:
  explicit ButtonGestures(uint16_t debounceMs = 30, uint16_t doubleClickWindowMs = 350)
      : debounceMs_(debounceMs), doubleClickWindowMs_(doubleClickWindowMs), rawPressed_(false), stablePressed_(false),
        clickPending_(false), secondPressCandidate_(false), rawChangedAt_(0), firstReleaseAt_(0) {}

  ButtonGestureEvent update(bool rawPressed, uint32_t nowMs);
  bool pressed() const { return stablePressed_; }

 private:
  static bool elapsed(uint32_t nowMs, uint32_t sinceMs, uint16_t intervalMs) {
    return static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs;
  }

  uint16_t debounceMs_;
  uint16_t doubleClickWindowMs_;
  bool rawPressed_;
  bool stablePressed_;
  bool clickPending_;
  bool secondPressCandidate_;
  uint32_t rawChangedAt_;
  uint32_t firstReleaseAt_;
};

JigglerMode nextJigglerMode(JigglerMode mode);
bool runButtonGestureSelfTest();
