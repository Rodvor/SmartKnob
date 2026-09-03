#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include <LittleFS.h>
#include <atomic>
#include <freertos/queue.h>

// Consumer key codes
#define KEY_MEDIA_VOLUME_UP      0xE9
#define KEY_MEDIA_VOLUME_DOWN    0xEA
#define KEY_MEDIA_PLAY_PAUSE     0xCD
#define KEY_MEDIA_NEXT_TRACK     0xB5
#define KEY_MEDIA_PREVIOUS_TRACK 0xB6

// HID report descriptor: keyboard + consumer control
static const uint8_t hidReportDescriptor[] = {
  // Keyboard
  0x05, 0x01,  // Usage Page: Generic Desktop
  0x09, 0x06,  // Usage: Keyboard
  0xA1, 0x01,  // Collection: Application
  0x85, 0x01,  //   Report ID: 1
  0x05, 0x07,  //   Usage Page: Keyboard
  0x19, 0xE0,  //   Usage Minimum: Left Control
  0x29, 0xE7,  //   Usage Maximum: Right GUI
  0x15, 0x00,  //   Logical Minimum: 0
  0x25, 0x01,  //   Logical Maximum: 1
  0x75, 0x01,  //   Report Size: 1
  0x95, 0x08,  //   Report Count: 8
  0x81, 0x02,  //   Input: Data, Variable, Absolute (modifier keys)
  0x95, 0x01,  //   Report Count: 1
  0x75, 0x08,  //   Report Size: 8
  0x81, 0x03,  //   Input: Constant (reserved)
  0x95, 0x06,  //   Report Count: 6
  0x75, 0x08,  //   Report Size: 8
  0x15, 0x00,  //   Logical Minimum: 0
  0x25, 0x73,  //   Logical Maximum: 115
  0x05, 0x07,  //   Usage Page: Keyboard
  0x19, 0x00,  //   Usage Minimum: 0
  0x29, 0x73,  //   Usage Maximum: 115
  0x81, 0x00,  //   Input: Data, Array
  0xC0,        // End Collection

  // Consumer Control
  0x05, 0x0C,  // Usage Page: Consumer
  0x09, 0x01,  // Usage: Consumer Control
  0xA1, 0x01,  // Collection: Application
  0x85, 0x02,  //   Report ID: 2
  0x15, 0x00,  //   Logical Minimum: 0
  0x26, 0xFF, 0x03, // Logical Maximum: 1023
  0x19, 0x00,  //   Usage Minimum: 0
  0x2A, 0xFF, 0x03, // Usage Maximum: 1023
  0x75, 0x10,  //   Report Size: 16
  0x95, 0x01,  //   Report Count: 1
  0x81, 0x00,  //   Input: Data, Array
  0xC0         // End Collection
};

// Modifier bitmasks
#define MOD_LEFT_CTRL   0x01
#define MOD_LEFT_SHIFT  0x02
#define MOD_LEFT_ALT    0x04
#define MOD_LEFT_GUI    0x08

// Keep Shift+Option held between volume ticks. They are released after this
// much time without a new volume turn.
static constexpr uint32_t VOLUME_MODIFIER_IDLE_RELEASE_MS = 500;

// Key codes (subset)
#define HID_KEY_F13  0x68
#define HID_KEY_F14  0x69
#define HID_KEY_F15  0x6A

class NimBLEKeyboard : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
  NimBLEServer*    pServer    = nullptr;
  NimBLEHIDDevice* hid        = nullptr;
  NimBLECharacteristic* input = nullptr;
  NimBLECharacteristic* consumer = nullptr;

  uint32_t connectedSince = 0;
  bool loggingReady = false;

  static const size_t MAX_LOG_SIZE = 8192;

  void logEvent(const char* msg) {
    if (!loggingReady) return;
    fs::File f = LittleFS.open("/ble_log.txt", "a");
    if (!f) return;
    // Check size and truncate if too large
    if (f.size() > MAX_LOG_SIZE) {
      f.close();
      LittleFS.remove("/ble_log.txt");
      f = LittleFS.open("/ble_log.txt", "a");
      if (!f) return;
      f.println("[log truncated]");
    }
    unsigned long s = millis() / 1000;
    unsigned long m = s / 60;
    unsigned long h = m / 60;
    f.printf("[%02lu:%02lu:%02lu] %s\n", h % 100, m % 60, s % 60, msg);
    f.close();
  }

  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    connected.store(false);
    keyboardSubscribed.store(false);
    consumerSubscribed.store(false);
    connectionGeneration.fetch_add(1);
    if (reportQueue) xQueueReset(reportQueue);
    connHandle.store(info.getConnHandle());
    connectedSince = millis();
    connected.store(true);
    Serial.println("BLE connected");
    logEvent("Connected");
    // Let the host choose connection parameters. Forcing a renegotiation here has
    // caused timeout disconnects with macOS.
  }

  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    connected.store(false);
    keyboardSubscribed.store(false);
    consumerSubscribed.store(false);
    connectionGeneration.fetch_add(1);
    if (reportQueue) xQueueReset(reportQueue);
    uint32_t duration = (millis() - connectedSince) / 1000;
    char buf[80];
    snprintf(buf, sizeof(buf), "Disconnected reason=0x%02X, duration=%lus", reason, duration);
    Serial.println(buf);
    logEvent(buf);
  }

  void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& info, uint16_t subValue) override {
    const bool notificationsEnabled = (subValue & 0x01) != 0;
    if (chr == input) {
      keyboardSubscribed.store(notificationsEnabled);
      if (notificationsEnabled) enqueueRelease(input, 8);
    } else if (chr == consumer) {
      consumerSubscribed.store(notificationsEnabled);
      if (notificationsEnabled) enqueueRelease(consumer, 2);
    }
  }

  void begin(const char* deviceName = "Haptic Knob") {
    if (LittleFS.begin(true)) {
      loggingReady = true;
      logEvent("Device booted");
    }

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this);
    pServer->advertiseOnDisconnect(true);

    hid = new NimBLEHIDDevice(pServer);
    hid->setManufacturer("DIY");
    hid->setPnp(0x02, 0x303A, 0x0001, 0x0100);  // Espressif VID
    hid->setHidInfo(0x00, 0x01);

    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

    input = hid->getInputReport(1);
    consumer = hid->getInputReport(2);
    input->setCallbacks(this);
    consumer->setCallbacks(this);

    uint8_t keyboardRelease[8] = {0};
    uint8_t consumerRelease[2] = {0};
    input->setValue(keyboardRelease, sizeof(keyboardRelease));
    consumer->setValue(consumerRelease, sizeof(consumerRelease));

    hid->startServices();

    reportQueue = xQueueCreate(32, sizeof(HidAction));
    xTaskCreatePinnedToCore(reportTask, "hid_report", 4096, this, 2, nullptr, 0);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    adv->start();

    Serial.println("BLE HID advertising started");
    logEvent("Advertising started");
  }

  bool isConnected() const { return connected.load(); }

  // Send keyboard report: modifier byte + up to 6 keycodes
  void sendKeyReport(uint8_t modifiers, uint8_t key1 = 0, uint8_t key2 = 0) {
    if (!connected.load() || !keyboardSubscribed.load() || !reportQueue) return;
    uint8_t report[8] = {modifiers, 0, key1, key2, 0, 0, 0, 0};
    enqueueAction(input, report, sizeof(report));
  }

  // Send consumer control key
  void sendConsumer(uint16_t key) {
    if (!connected.load() || !consumerSubscribed.load() || !reportQueue) return;
    uint8_t report[2] = {(uint8_t)(key & 0xFF), (uint8_t)(key >> 8)};
    enqueueAction(consumer, report, sizeof(report));
  }

  void volumeUp()   { sendVolumeKey(KEY_MEDIA_VOLUME_UP); }
  void volumeDown() { sendVolumeKey(KEY_MEDIA_VOLUME_DOWN); }

  void playPause() {
    sendConsumer(KEY_MEDIA_PLAY_PAUSE);
  }

  void nextTrack() {
    sendConsumer(KEY_MEDIA_NEXT_TRACK);
  }

  void prevTrack() {
    sendConsumer(KEY_MEDIA_PREVIOUS_TRACK);
  }

  // F13 = mute
  void pressF13() {
    sendKeyReport(0x00, HID_KEY_F13);
  }

  // Alt+F13 = deafen
  void pressAltF13() {
    sendKeyReport(MOD_LEFT_ALT, HID_KEY_F13);
  }

  // Shift+Alt+F13 = disconnect
  void pressShiftAltF13() {
    sendKeyReport(MOD_LEFT_SHIFT | MOD_LEFT_ALT, HID_KEY_F13);
  }

  // F14 = brightness down
  void pressF14() {
    sendKeyReport(0x00, HID_KEY_F14);
  }

  // F15 = brightness up
  void pressF15() {
    sendKeyReport(0x00, HID_KEY_F15);
  }

private:
  struct HidAction {
    NimBLECharacteristic* chr;
    uint8_t data[8];
    uint8_t len;
    uint32_t generation;
    bool releaseAfter;
    uint8_t modifiers;
    uint32_t queuedAt;
  };

  std::atomic<bool> connected{false};
  std::atomic<bool> keyboardSubscribed{false};
  std::atomic<bool> consumerSubscribed{false};
  std::atomic<uint16_t> connHandle{BLE_HS_CONN_HANDLE_NONE};
  std::atomic<uint32_t> connectionGeneration{0};
  QueueHandle_t reportQueue = nullptr;

  static void reportTask(void* param) {
    NimBLEKeyboard* self = (NimBLEKeyboard*)param;
    HidAction action;
    HidAction modifierAction{};
    bool modifiersHeld = false;
    uint8_t heldModifiers = 0;

    auto releaseModifiers = [&]() {
      if (!modifiersHeld) return;
      uint8_t keyboardRelease[8] = {0};
      self->notifyWhenReady(modifierAction, self->input,
                            keyboardRelease, sizeof(keyboardRelease));
      modifiersHeld = false;
      heldModifiers = 0;
    };

    while (true) {
      TickType_t waitTicks = portMAX_DELAY;
      if (modifiersHeld) {
        const uint32_t idleMs = millis() - modifierAction.queuedAt;
        if (idleMs >= VOLUME_MODIFIER_IDLE_RELEASE_MS) {
          waitTicks = 0;
        } else {
          waitTicks = pdMS_TO_TICKS(VOLUME_MODIFIER_IDLE_RELEASE_MS - idleMs);
          if (waitTicks == 0) waitTicks = 1;
        }
      }

      if (xQueueReceive(self->reportQueue, &action, waitTicks) != pdTRUE) {
        releaseModifiers();
        continue;
      }

      if (!self->actionIsCurrent(action)) {
        if (modifiersHeld &&
            !self->reportIsReady(modifierAction, self->input)) {
          // A reconnect will send a neutral report when macOS subscribes again.
          modifiersHeld = false;
          heldModifiers = 0;
        }
        continue;
      }

      // Never let the fine-volume modifiers affect another kind of action.
      if (modifiersHeld &&
          (action.modifiers == 0 || action.modifiers != heldModifiers ||
           action.generation != modifierAction.generation)) {
        releaseModifiers();
        vTaskDelay(pdMS_TO_TICKS(4));
      }

      if (action.modifiers != 0) {
        if (!modifiersHeld) {
          uint8_t modifierReport[8] = {action.modifiers, 0, 0, 0, 0, 0, 0, 0};
          if (!self->notifyWhenReady(action, self->input,
                                     modifierReport, sizeof(modifierReport))) {
            continue;
          }
          modifiersHeld = true;
          heldModifiers = action.modifiers;
          vTaskDelay(pdMS_TO_TICKS(8));
        }

        // Use the time the knob event was detected, rather than the time it
        // happened to reach the front of the BLE queue.
        modifierAction = action;
      }

      const bool pressSent = self->notifyWhenReady(action, action.chr, action.data, action.len);
      if (!pressSent) {
        if (action.modifiers != 0) releaseModifiers();
        continue;
      }

      if (!action.releaseAfter) continue;

      // Keep press and release as distinct HID reports. Once a press has been
      // accepted, do not process another action until its release is accepted.
      vTaskDelay(pdMS_TO_TICKS(8));
      uint8_t release[8] = {0};
      self->notifyWhenReady(action, action.chr, release, action.len);
      vTaskDelay(pdMS_TO_TICKS(4));
    }
  }

  bool reportIsReady(const HidAction& action, NimBLECharacteristic* chr) const {
    if (!connected.load() || action.generation != connectionGeneration.load()) return false;
    if (chr == input) return keyboardSubscribed.load();
    if (chr == consumer) return consumerSubscribed.load();
    return false;
  }

  bool actionIsCurrent(const HidAction& action) const {
    if (!reportIsReady(action, action.chr)) return false;
    return action.modifiers == 0 || reportIsReady(action, input);
  }

  bool notifyWhenReady(const HidAction& action, NimBLECharacteristic* chr,
                       const uint8_t* data, uint8_t len) {
    while (reportIsReady(action, chr)) {
      const uint16_t handle = connHandle.load();
      if (chr->notify(data, len, handle)) return true;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
  }

  void enqueueAction(NimBLECharacteristic* chr, const uint8_t* data, uint8_t len,
                     uint8_t modifiers = 0) {
    if (!reportQueue) return;
    HidAction action{};
    action.chr = chr;
    memcpy(action.data, data, len);
    action.len = len;
    action.generation = connectionGeneration.load();
    action.releaseAfter = true;
    action.modifiers = modifiers;
    action.queuedAt = millis();
    xQueueSend(reportQueue, &action, 0);
  }

  void enqueueRelease(NimBLECharacteristic* chr, uint8_t len) {
    if (!connected.load() || !reportQueue) return;
    HidAction action{};
    action.chr = chr;
    action.len = len;
    action.generation = connectionGeneration.load();
    action.releaseAfter = false;
    xQueueSendToFront(reportQueue, &action, 0);
  }

  void sendVolumeKey(uint16_t key) {
    if (!connected.load() || !keyboardSubscribed.load() ||
        !consumerSubscribed.load() || !reportQueue) return;
    uint8_t report[2] = {(uint8_t)(key & 0xFF), (uint8_t)(key >> 8)};
    enqueueAction(consumer, report, sizeof(report), MOD_LEFT_SHIFT | MOD_LEFT_ALT);
  }
};
