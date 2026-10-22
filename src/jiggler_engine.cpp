#include "jiggler_engine.h"

#include <math.h>

namespace {
constexpr uint32_t kTickMs = 12;
constexpr int32_t kMinimumScreenWidth = 320;
constexpr int32_t kMinimumScreenHeight = 240;
constexpr int32_t kMaximumScreenDimension = 32767;

JigglerProfile profileFor(JigglerMode mode) {
  switch (mode) {
    case JigglerMode::Medium: return {1800, 3000, 7000};
    case JigglerMode::Fast: return {700, 500, 2000};
    case JigglerMode::Slow:
    default: return {3500, 8000, 15000};
  }
}

float bezier(float p0, float p1, float p2, float p3, float t) {
  const float u = 1.0f - t;
  const float uu = u * u;
  const float tt = t * t;
  return uu * u * p0 + 3.0f * uu * t * p1 + 3.0f * u * tt * p2 + tt * t * p3;
}
}  // namespace

JigglerEngine::JigglerEngine(JigglerRandom randomFn, void *randomContext)
    : randomFn_(randomFn), randomContext_(randomContext), mode_(JigglerMode::Slow), active_(false), moving_(false),
      screenWidth_(1920), screenHeight_(1080), virtualX_(960), virtualY_(540), pathStartX_(960), pathStartY_(540),
      targetX_(960), targetY_(540), lastTargetX_(960), lastTargetY_(540), control1X_(960), control1Y_(540),
      control2X_(960), control2Y_(540), pathStartedAt_(0), pauseUntil_(0), nextTickAt_(0) {}

bool JigglerEngine::setMode(JigglerMode mode, uint32_t nowMs) {
  if (mode != JigglerMode::Slow && mode != JigglerMode::Medium && mode != JigglerMode::Fast) return false;
  const bool changed = mode_ != mode;
  mode_ = mode;
  if (changed && active_) {
    moving_ = false;
    pauseUntil_ = nowMs;
    nextTickAt_ = nowMs;
  }
  return true;
}

bool JigglerEngine::setScreen(uint16_t width, uint16_t height, uint32_t nowMs) {
  if (width < kMinimumScreenWidth || height < kMinimumScreenHeight || width > kMaximumScreenDimension ||
      height > kMaximumScreenDimension) return false;
  screenWidth_ = width;
  screenHeight_ = height;
  virtualX_ = clampPosition(static_cast<float>(virtualX_), screenWidth_);
  virtualY_ = clampPosition(static_cast<float>(virtualY_), screenHeight_);
  moving_ = false;
  pauseUntil_ = nowMs;
  nextTickAt_ = nowMs;
  return true;
}

void JigglerEngine::setActive(bool active, uint32_t nowMs) {
  if (active_ == active) return;
  active_ = active;
  moving_ = false;
  nextTickAt_ = nowMs;
  pauseUntil_ = nowMs;
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

uint32_t JigglerEngine::randomBelow(uint32_t upperExclusive) {
  if (upperExclusive == 0) return 0;
  return randomFn_ ? randomFn_(upperExclusive, randomContext_) % upperExclusive : 0;
}

int32_t JigglerEngine::randomSigned(int32_t magnitude) {
  if (magnitude <= 0) return 0;
  const uint32_t span = static_cast<uint32_t>(magnitude) * 2u + 1u;
  return static_cast<int32_t>(randomBelow(span)) - magnitude;
}

int32_t JigglerEngine::clampPosition(float value, uint16_t limit) {
  const int32_t rounded = static_cast<int32_t>(lroundf(value));
  if (rounded < 0) return 0;
  if (rounded >= limit) return static_cast<int32_t>(limit) - 1;
  return rounded;
}

void JigglerEngine::startPath(uint32_t nowMs) {
  pathStartX_ = virtualX_;
  pathStartY_ = virtualY_;

  const uint32_t minimumDistance = (screenWidth_ < screenHeight_ ? screenWidth_ : screenHeight_) / 5u;
  targetX_ = pathStartX_;
  targetY_ = pathStartY_;
  for (uint8_t attempt = 0; attempt < 16; ++attempt) {
    targetX_ = static_cast<int32_t>(randomBelow(screenWidth_));
    targetY_ = static_cast<int32_t>(randomBelow(screenHeight_));
    const int64_t dx = static_cast<int64_t>(targetX_) - pathStartX_;
    const int64_t dy = static_cast<int64_t>(targetY_) - pathStartY_;
    if (dx * dx + dy * dy >= static_cast<int64_t>(minimumDistance) * minimumDistance) break;
  }
  if (targetX_ == pathStartX_ && targetY_ == pathStartY_) {
    targetX_ = (pathStartX_ < static_cast<int32_t>(screenWidth_ / 2)) ? screenWidth_ - 1 : 0;
  }

  const float oneThirdX = (targetX_ - pathStartX_) / 3.0f;
  const float oneThirdY = (targetY_ - pathStartY_) / 3.0f;
  const int32_t curveX = screenWidth_ / 6;
  const int32_t curveY = screenHeight_ / 6;
  control1X_ = pathStartX_ + oneThirdX + randomSigned(curveX);
  control1Y_ = pathStartY_ + oneThirdY + randomSigned(curveY);
  control2X_ = pathStartX_ + 2.0f * oneThirdX + randomSigned(curveX);
  control2Y_ = pathStartY_ + 2.0f * oneThirdY + randomSigned(curveY);
  control1X_ = clampPosition(control1X_, screenWidth_);
  control1Y_ = clampPosition(control1Y_, screenHeight_);
  control2X_ = clampPosition(control2X_, screenWidth_);
  control2Y_ = clampPosition(control2Y_, screenHeight_);

  lastTargetX_ = pathStartX_;
  lastTargetY_ = pathStartY_;
  pathStartedAt_ = nowMs;
  moving_ = true;
}

bool JigglerEngine::update(uint32_t nowMs, int32_t &deltaX, int32_t &deltaY) {
  deltaX = 0;
  deltaY = 0;
  if (!active_ || static_cast<int32_t>(nowMs - nextTickAt_) < 0) return false;
  nextTickAt_ = nowMs + kTickMs;

  if (!moving_) {
    if (static_cast<int32_t>(nowMs - pauseUntil_) < 0) return false;
    startPath(nowMs);
  }

  const JigglerProfile current = profile();
  const uint32_t elapsed = nowMs - pathStartedAt_;
  const float rawProgress = elapsed >= current.trajectoryMs ? 1.0f : static_cast<float>(elapsed) / current.trajectoryMs;
  const float eased = rawProgress * rawProgress * (3.0f - 2.0f * rawProgress);
  const int32_t plannedX = clampPosition(bezier(pathStartX_, control1X_, control2X_, targetX_, eased), screenWidth_);
  const int32_t plannedY = clampPosition(bezier(pathStartY_, control1Y_, control2Y_, targetY_, eased), screenHeight_);

  deltaX = plannedX - lastTargetX_;
  deltaY = plannedY - lastTargetY_;
  lastTargetX_ = plannedX;
  lastTargetY_ = plannedY;
  virtualX_ = plannedX;
  virtualY_ = plannedY;

  if (rawProgress >= 1.0f) {
    moving_ = false;
    const JigglerProfile p = profile();
    const uint32_t pauseRange = p.pauseMaxMs - p.pauseMinMs + 1u;
    pauseUntil_ = nowMs + p.pauseMinMs + randomBelow(pauseRange);
  }
  return deltaX != 0 || deltaY != 0;
}
