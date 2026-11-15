// Host-only regression checks for observed BLE callback orderings.
// These simulate host events, not radio delivery or FreeRTOS scheduling.
#include "platform.h"
#define private public
#include "../src/ESP32/NimbleKeyboard.h"
#undef private
int main() {
  NimBLEKeyboard knob;
  assert(knob.begin());
  NimBLEConnInfo info;
  const int writes = Serial.writes;
  // The actual failure: authenticated + subscriptions, with no onConnect.
  knob.onAuthenticationComplete(info);
  knob.onSubscribe(knob.input, info, 1);
  knob.onSubscribe(knob.consumer, info, 1);
  assert(knob.isConnected() && knob.isReady());
  assert(Serial.writes == writes); // Host callbacks never perform synchronous I/O.
  auto gen = knob.connectionGeneration.load();
  auto queued = uxQueueMessagesWaiting(knob.reportQueue);
  knob.onConnect(knob.pServer, info);
  assert(knob.isReady());
  assert(knob.connectionGeneration.load() == gen &&
         uxQueueMessagesWaiting(knob.reportQueue) == queued);
  // Disconnect clears readiness. Subscriptions alone can restore the next link.
  knob.onDisconnect(knob.pServer, info, 0x208);
  assert(!knob.isReady());
  knob.onSubscribe(knob.input, info, 1);
  knob.onSubscribe(knob.consumer, info, 1);
  assert(knob.isReady());
  assert(knob.connectionGeneration.load() != gen);
  // Stale report from a previous connection is never sent into the next one.
  NimBLEKeyboard::HidAction old{};
  old.chr = knob.input;
  old.generation = gen;
  assert(!knob.actionIsCurrent(old));
  // A missing HID subscription triggers recovery, even before setup completes.
  knob.onDisconnect(knob.pServer, info, 0x208);
  info.encrypted = false;
  knob.onConnect(knob.pServer, info);
  assert(!knob.isReady());
  testNow = 16000;
  knob.poll();
  assert(knob.pServer->disconnects == 1);
  knob.onDisconnect(knob.pServer, info, 0x216);
  NimBLEDevice::getAdvertising()->active = false;
  testNow += 1000;
  knob.poll();
  assert(NimBLEDevice::getAdvertising()->active);
  // Parameter negotiation is delayed and attempted only once per connection.
  info.encrypted = true;
  knob.onAuthenticationComplete(info);
  knob.onSubscribe(knob.input, info, 1);
  knob.onSubscribe(knob.consumer, info, 1);
  testNow += 1000;
  knob.poll();
  assert(knob.pServer->updates == 0);
  testNow += 5000;
  knob.poll();
  assert(knob.pServer->updates == 1);
  testNow += 5000;
  knob.poll();
  assert(knob.pServer->updates == 1);
  // Failed key-up must block new presses until a reconnect clears recovery.
  NimBLEKeyboard::HidAction current{};
  current.chr = knob.consumer;
  current.generation = knob.connectionGeneration.load();
  uint8_t release[2] = {};
  testNotifyAccepted = false;
  knob.releaseOrRecover(current, knob.consumer, release, sizeof(release));
  assert(!knob.isReady() && !knob.actionIsCurrent(current));
  testNotifyAccepted = true;
  // A missing disconnect callback must not leave a ghost connection forever.
  testLinkPresent = false;
  testNow += 1000;
  knob.poll();
  assert(!knob.isConnected());
  puts("PASS: reordered/missing connect events, subscription restore, stale reports, "
       "incomplete-link recovery, advertising recovery, deferred single parameter request, "
       "nonblocking callback logging, failed key-up recovery, missing disconnect");
}
