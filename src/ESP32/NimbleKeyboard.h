#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include <LittleFS.h>
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

// Key codes (subset)
#define HID_KEY_F13  0x68
#define HID_KEY_F14  0x69
#define HID_KEY_F15  0x6A

class NimBLEKeyboard : public NimBLEServerCallbacks {
public:
  NimBLEServer*    pServer    = nullptr;
  NimBLEHIDDevice* hid        = nullptr;
  NimBLECharacteristic* input = nullptr;
  NimBLECharacteristic* consumer = nullptr;
  bool connected = false;

  uint16_t connHandle = 0;
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
    connected = true;
    connHandle = info.getConnHandle();
    connectedSince = millis();
    Serial.println("BLE connected");
    logEvent("Connected");
    // Let macOS choose its own connection parameters — requesting specific params
    // causes renegotiation conflicts that produce 0x222 timeouts and temporary slowness.
    pServer->updateConnParams(info.getConnHandle(), 12, 24, 0, 400); // Test
  }

  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    connected = false;
    uint32_t duration = (millis() - connectedSince) / 1000;
    char buf[80];
    snprintf(buf, sizeof(buf), "Disconnected reason=0x%02X, duration=%lus", reason, duration);
    Serial.println(buf);
    logEvent(buf);
    NimBLEDevice::startAdvertising();
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

    hid = new NimBLEHIDDevice(pServer);
    hid->setManufacturer("DIY");
    hid->setPnp(0x02, 0x303A, 0x0001, 0x0100);  // Espressif VID
    hid->setHidInfo(0x00, 0x01);

    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

    input = hid->getInputReport(1);
    consumer = hid->getInputReport(2);

    hid->startServices();

    reportQueue = xQueueCreate(64, sizeof(HidReport));
    xTaskCreatePinnedToCore(reportTask, "hid_report", 4096, this, 2, nullptr, 0);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    adv->start();

    Serial.println("BLE HID advertising started");
    logEvent("Advertising started");
  }

  bool isConnected() { return connected; }

  // Send keyboard report: modifier byte + up to 6 keycodes
  void sendKeyReport(uint8_t modifiers, uint8_t key1 = 0, uint8_t key2 = 0) {
    if (!connected || !reportQueue) return;
    if (uxQueueSpacesAvailable(reportQueue) < 2) return; // never send a press without its release
    uint8_t report[8] = {modifiers, 0, key1, key2, 0, 0, 0, 0};
    enqueueReport(input, report, sizeof(report));
    uint8_t release[8] = {0};
    enqueueReport(input, release, sizeof(release));
  }
  // Send consumer control key
  void sendConsumer(uint16_t key) {
    if (!connected || !reportQueue) return;
    if (uxQueueSpacesAvailable(reportQueue) < 2) return;
    uint8_t report[2] = {(uint8_t)(key & 0xFF), (uint8_t)(key >> 8)};
    enqueueReport(consumer, report, sizeof(report));
    uint8_t release[2] = {0, 0};
    enqueueReport(consumer, release, sizeof(release));
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
  struct HidReport {
    NimBLECharacteristic* chr;
    uint8_t data[8];
    uint8_t len;
  };

  QueueHandle_t reportQueue = nullptr;

  static void reportTask(void* param) {
    NimBLEKeyboard* self = (NimBLEKeyboard*)param;
    HidReport r;
    while (true) {
      if (xQueueReceive(self->reportQueue, &r, portMAX_DELAY) == pdTRUE) {
        if (!self->connected) continue;
        r.chr->setValue(r.data, r.len);
        int attempts = 0;
        while (!r.chr->notify() && attempts < 20) { // retry until it actually sends
          vTaskDelay(pdMS_TO_TICKS(2));
          attempts++;
        }
        vTaskDelay(pdMS_TO_TICKS(4)); // small settle gap so press/release stay distinct reports
      }
    }
  }

  void enqueueReport(NimBLECharacteristic* chr, const uint8_t* data, uint8_t len) {
    if (!reportQueue) return;
    HidReport r;
    r.chr = chr;
    memcpy(r.data, data, len);
    r.len = len;
    xQueueSend(reportQueue, &r, 0); // non-blocking, safe from ISR-adjacent callers
  }

  void sendVolumeKey(uint16_t key) {
    if (!connected || !reportQueue) return;
    if (uxQueueSpacesAvailable(reportQueue) < 4) return; // full 4-report sequence or nothing
    uint8_t mod[8] = {MOD_LEFT_SHIFT | MOD_LEFT_ALT, 0, 0, 0, 0, 0, 0, 0};
    enqueueReport(input, mod, sizeof(mod));
    sendConsumer(key);
    uint8_t release[8] = {0};
    enqueueReport(input, release, sizeof(release));
  }
};
