#pragma once

#include <stdint.h>

#include "jiggler_engine.h"

class StatusLedIndicator {
 public:
  static constexpr uint32_t kVisibleMs = 4000;

  void wake(uint32_t nowMs) {
    awake_ = true;
    startedAt_ = nowMs;
  }

  void stop() { awake_ = false; }

  uint32_t remainingMs(uint32_t nowMs) const {
    const uint32_t age = nowMs - startedAt_;
    return awake_ && age < kVisibleMs ? kVisibleMs - age : 0;
  }

  bool on(uint32_t nowMs, JigglerMode mode, bool jigglerActive) {
    if (!jigglerActive) { stop(); return false; }
    if (!remainingMs(nowMs)) { awake_ = false; return false; }
    const uint32_t period = mode == JigglerMode::Slow ? 1200 : mode == JigglerMode::Medium ? 500 : 180;
    return ((nowMs - startedAt_) % period) < period / 2;
  }

 private:
  bool awake_ = false;
  uint32_t startedAt_ = 0;
};

bool runStatusLedSelfTest();
