#include <cassert>
#include <cstdio>
#include "../src/ESP32/TouchFilter.h"
int main() {
  TouchFilter t;
  assert(t.update(false, 0, 0, 0) == TouchFilter::None);
  assert(t.update(true, 1, 100, 10) == TouchFilter::None);
  assert(t.update(true, 0, 100, 20) == TouchFilter::Tap); // Short tap after idle read failure.
  assert(t.update(true, 0, 100, 30) == TouchFilter::None); // No repeated action.
  t.update(true, 1, 100, 40);
  assert(t.update(true, 0, 100, 50) == TouchFilter::None); // Debounce.
  t.update(true, 1, 10, 400);
  t.update(true, 1, 120, 1600);
  assert(t.update(true, 0, 120, 1700) == TouchFilter::SwipeRight); // Slow swipe.
  t.update(true, 1, 200, 2100);
  t.update(true, 1, 50, 2200);
  assert(t.update(true, 0, 50, 2300) == TouchFilter::SwipeLeft);
  t.update(true, 1, 100, 2700);
  assert(t.update(false, 0, 0, 2710) == TouchFilter::None); // Error isn't a release.
  t.update(true, 1, 100, 2720);
  assert(t.update(true, 0, 100, 2730) == TouchFilter::None); // Interrupted gesture discarded.
  t.update(true, 1, 100, 2800);
  assert(t.update(true, 0, 100, 2810) == TouchFilter::Tap); // Recovery doesn't stay latched.
  puts("PASS: touch after idle failures, short taps, slow swipes, debounce, interrupted gestures, recovery");
}
