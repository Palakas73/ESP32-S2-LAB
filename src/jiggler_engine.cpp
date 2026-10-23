#include "jiggler_engine.h"

#include <math.h>

namespace {
constexpr uint32_t kTickMs = 8;
constexpr int32_t kMinimumScreenWidth = 320;
constexpr int32_t kMinimumScreenHeight = 240;
constexpr int32_t kMaximumScreenDimension = 32767;
constexpr float kPi = 3.14159265359f;
constexpr float kRadiusSettleFraction = 0.15f;

JigglerProfile profileFor(JigglerMode mode) {
  switch (mode) {
    case JigglerMode::Slow: return {8000, 0, 0};
    case JigglerMode::Medium: return {6000, 0, 0};
    case JigglerMode::Fast: return {4000, 0, 0};
    default: return {6000, 0, 0};
  }
}

float smoothstep(float value) {
  const float t = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  return t * t * (3.0f - 2.0f * t);
}
}  // namespace

JigglerEngine::JigglerEngine()
    : mode_(JigglerMode::Medium), active_(false), moving_(false),
      screenWidth_(1920), screenHeight_(1080), virtualX_(1059), virtualY_(539), lastTargetX_(1059), lastTargetY_(539),
      circleCenterX_(959.0f), circleCenterY_(539.0f), pathStartAngle_(0.0f), pathStartRadius_(0.0f),
      pathStartedAt_(0), nextTickAt_(0) {}

bool JigglerEngine::setMode(JigglerMode mode, uint32_t nowMs) {
  if (mode != JigglerMode::Slow && mode != JigglerMode::Medium && mode != JigglerMode::Fast) return false;
  const bool changed = mode_ != mode;
  mode_ = mode;
  if (changed && active_) {
    moving_ = false;
    nextTickAt_ = nowMs;
  }
  return true;
}

bool JigglerEngine::setScreen(uint16_t width, uint16_t height, uint32_t nowMs) {
  if (width < kMinimumScreenWidth || height < kMinimumScreenHeight || width > kMaximumScreenDimension ||
      height > kMaximumScreenDimension) return false;
  screenWidth_ = width;
  screenHeight_ = height;
  circleCenterX_ = static_cast<float>((static_cast<int32_t>(screenWidth_) - 1) / 2);
  circleCenterY_ = static_cast<float>((static_cast<int32_t>(screenHeight_) - 1) / 2);
  // The USB pointer starts at the current host position. Assign its virtual
  // point to the ring without sending a 100 px radial jump on activation.
  virtualX_ = static_cast<int32_t>(lroundf(circleCenterX_ + circleRadiusPx()));
  virtualY_ = static_cast<int32_t>(lroundf(circleCenterY_));
  lastTargetX_ = virtualX_;
  lastTargetY_ = virtualY_;
  moving_ = false;
  nextTickAt_ = nowMs;
  return true;
}

void JigglerEngine::setActive(bool active, uint32_t nowMs) {
  if (active_ == active) return;
  if (active) {
    // The host cursor cannot be read over HID. Rebase the virtual coordinate
    // frame so every activation begins and ends at the same physical point.
    virtualX_ = static_cast<int32_t>(lroundf(circleCenterX_ + circleRadiusPx()));
    virtualY_ = static_cast<int32_t>(lroundf(circleCenterY_));
    lastTargetX_ = virtualX_;
    lastTargetY_ = virtualY_;
  }
  active_ = active;
  moving_ = false;
  nextTickAt_ = nowMs;
}

void JigglerEngine::stopAt(int32_t x, int32_t y) {
  active_ = false;
  moving_ = false;
  virtualX_ = clampPosition(static_cast<float>(x), screenWidth_);
  virtualY_ = clampPosition(static_cast<float>(y), screenHeight_);
  lastTargetX_ = virtualX_;
  lastTargetY_ = virtualY_;
}

JigglerProfile JigglerEngine::profile() const { return profileFor(mode_); }

int32_t JigglerEngine::clampPosition(float value, uint16_t limit) {
  const int32_t rounded = static_cast<int32_t>(lroundf(value));
  if (rounded < 0) return 0;
  if (rounded >= limit) return static_cast<int32_t>(limit) - 1;
  return rounded;
}

void JigglerEngine::startPath(uint32_t nowMs) {
  const float fromCenterX = static_cast<float>(virtualX_) - circleCenterX_;
  const float fromCenterY = static_cast<float>(virtualY_) - circleCenterY_;
  pathStartRadius_ = sqrtf(fromCenterX * fromCenterX + fromCenterY * fromCenterY);
  pathStartAngle_ = pathStartRadius_ > 0.01f ? atan2f(fromCenterY, fromCenterX) : 0.0f;
  lastTargetX_ = virtualX_;
  lastTargetY_ = virtualY_;
  pathStartedAt_ = nowMs;
  moving_ = true;
}

bool JigglerEngine::update(uint32_t nowMs, int32_t &deltaX, int32_t &deltaY) {
  deltaX = 0;
  deltaY = 0;
  if (!active_ || static_cast<int32_t>(nowMs - nextTickAt_) < 0) return false;
  nextTickAt_ = nowMs + kTickMs;

  if (!moving_) {
    startPath(nowMs);
  }

  const JigglerProfile current = profile();
  const uint32_t elapsed = nowMs - pathStartedAt_;
  const float rawProgress = elapsed >= current.trajectoryMs ? 1.0f : static_cast<float>(elapsed) / current.trajectoryMs;
  // Matching acceleration on opposite semicircles keeps their relative HID
  // deltas paired even when the host applies a nonlinear pointer gain.
  const bool secondHalf = rawProgress >= 0.5f;
  const float halfProgress = secondHalf ? (rawProgress - 0.5f) * 2.0f : rawProgress * 2.0f;
  const float angle = pathStartAngle_ + kPi * ((secondHalf ? 1.0f : 0.0f) + smoothstep(halfProgress));
  const float radiusSettleProgress = rawProgress / kRadiusSettleFraction;
  const float radius = pathStartRadius_ +
                       (static_cast<float>(circleRadiusPx()) - pathStartRadius_) * smoothstep(radiusSettleProgress);
  const int32_t plannedX = clampPosition(circleCenterX_ + radius * cosf(angle), screenWidth_);
  const int32_t plannedY = clampPosition(circleCenterY_ + radius * sinf(angle), screenHeight_);

  deltaX = plannedX - lastTargetX_;
  deltaY = plannedY - lastTargetY_;
  lastTargetX_ = plannedX;
  lastTargetY_ = plannedY;
  virtualX_ = plannedX;
  virtualY_ = plannedY;

  if (rawProgress >= 1.0f) {
    // Close this turn, then schedule the next on the same time line. Keep
    // MOVING true and carry any tick overshoot into the following turn.
    startPath(pathStartedAt_ + current.trajectoryMs);
  }
  return deltaX != 0 || deltaY != 0;
}
