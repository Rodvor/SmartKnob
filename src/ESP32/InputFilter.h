#pragma once
#include <cmath>

// Track unwrapped detents with a deadband around each boundary. Reversing
// across a boundary requires deliberate travel, not encoder or motor jitter.
class DetentFilter {
public:
  void reset(float angle, int notches) {
    count = notches;
    step = 6.28318530718f / count;
    index = std::lround((angle + 1.57079632679f) / step);
  }
  int update(float angle) {
    if (!std::isfinite(angle)) return 0;
    const float position = (angle + 1.57079632679f) / step;
    const float delta = position - index;
    if (std::fabs(delta) < 0.65f) return 0;
    const long next = std::lround(position);
    const int direction = next > index ? 1 : -1;
    index = next;
    return direction;
  }
  int choice() const { return ((index % count) + count) % count; }
private:
  int count = 30;
  float step = 6.28318530718f / 30;
  long index = 0;
};
