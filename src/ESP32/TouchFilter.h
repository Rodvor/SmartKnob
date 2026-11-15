#pragma once
#include <cstdint>

class TouchFilter {
public:
  enum Gesture { None, Tap, SwipeRight, SwipeLeft };
  Gesture update(bool valid, uint8_t fingers, int x, uint32_t now) {
    if (!valid) {
      // An idle/sleeping controller must not block the next touch. If a
      // gesture was interrupted, discard it until a real release arrives.
      if (active) waitForRelease = true;
      active = false;
      return None;
    }
    if (waitForRelease) {
      if (fingers == 0) waitForRelease = false;
      return None;
    }
    if (fingers) {
      if (!active) { active = true; startX = x; }
      endX = x;
      return None;
    }
    if (!active) return None;
    active = false;
    if (hasGesture && now - lastGesture < 300) return None;
    hasGesture = true;
    lastGesture = now;
    const int dx = endX - startX;
    return dx > 40 ? SwipeRight : dx < -40 ? SwipeLeft : Tap;
  }
private:
  bool active = false, waitForRelease = false, hasGesture = false;
  int startX = 0, endX = 0;
  uint32_t lastGesture = 0;
};
