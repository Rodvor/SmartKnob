#include "platform.h"
#define private public
#include "../src/ESP32/NimbleKeyboard.h"
#undef private
#include "../src/ESP32/InputFilter.h"

int main() {
  DetentFilter filter;
  const float step = 6.28318530718f / 30;
  auto angle = [=](float n) { return n * step - 1.57079632679f; };
  filter.reset(angle(0), 30);
  for (float n : {0.49f, 0.51f, 0.48f, 0.60f}) assert(filter.update(angle(n)) == 0);
  assert(filter.update(angle(0.7f)) == 1 && filter.choice() == 1);
  for (float n : {0.51f, 0.49f, 0.4f}) assert(filter.update(angle(n)) == 0);
  assert(filter.update(angle(0.3f)) == -1 && filter.choice() == 0);
  filter.reset(angle(29), 30);
  assert(filter.update(angle(30)) == 1 && filter.choice() == 0);
  assert(filter.update(angle(29)) == -1 && filter.choice() == 29);
  filter.reset(angle(-1), 30);
  assert(filter.choice() == 29);
  assert(filter.update(NAN) == 0);
  filter.reset(-1.57079632679f, 3);
  assert(filter.update(-1.57079632679f + 0.1f) == 0);
  assert(filter.update(-1.57079632679f + 2.09439510239f) == 1);
  assert(filter.choice() == 1);

  NimBLEKeyboard knob;
  assert(knob.begin());
  NimBLEConnInfo info;
  knob.onAuthenticationComplete(info);
  knob.onSubscribe(knob.input, info, 1);
  knob.onSubscribe(knob.consumer, info, 1);
  knob.cancelPendingInputs();
  NimBLEKeyboard::HidAction action;
  auto pop = [&]() { assert(xQueueReceive(knob.reportQueue, &action, 0)); };
  knob.volumeUp(); pop(); knob.processAction(action);
  assert(testReports.size() == 4);
  assert(testReports[0].chr == knob.input && testReports[0].data[0] == 6);
  assert(testReports[1].chr == knob.consumer && testReports[1].data[0] == KEY_MEDIA_VOLUME_UP);
  assert(testReports[2].chr == knob.consumer && testReports[2].data[0] == 0);
  assert(testReports[3].chr == knob.input && testReports[3].data == std::vector<uint8_t>(8));
  testReports.clear();
  knob.pressAltF13(); pop(); knob.processAction(action);
  assert(testReports.size() == 2 && testReports[0].data[0] == MOD_LEFT_ALT);
  assert(testReports[1].data == std::vector<uint8_t>(8));
  testReports.clear();
  knob.nextTrack(); pop(); testNow += HID_ACTION_MAX_AGE_MS + 1; knob.processAction(action);
  assert(testReports.empty());
  for (int i = 0; i < 20; ++i) knob.volumeDown();
  assert(uxQueueMessagesWaiting(knob.reportQueue) == 8);
  pop(); knob.processAction(action); // Full queue cannot prevent this release.
  assert(testReports.size() == 4 && testReports.back().data == std::vector<uint8_t>(8));
  knob.cancelPendingInputs();
  assert(uxQueueMessagesWaiting(knob.reportQueue) == 0);
  testReports.clear();
  knob.volumeUp(); pop(); testFailConsumerRelease = true; knob.processAction(action);
  assert(!knob.isReady());
  assert(testReports.back().chr == knob.input && testReports.back().data == std::vector<uint8_t>(8));
  knob.volumeUp();
  assert(uxQueueMessagesWaiting(knob.reportQueue) == 0);
  testFailConsumerRelease = false;
  knob.onDisconnect(knob.pServer, info, 0x216);
  knob.onSubscribe(knob.input, info, 1);
  knob.onSubscribe(knob.consumer, info, 1);
  knob.cancelPendingInputs();
  knob.onStatus(knob.input, info, 7);
  assert(!knob.isReady());
  puts("PASS: detent jitter, reversal, wraparound, complete press/release sequences, stale input, queue overflow, cancellation, release failure and async error recovery");
}
