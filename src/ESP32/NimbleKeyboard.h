#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>

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

  // Newer NimBLE passes connInfo by reference, not just server pointer
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    connected = true;
    Serial.println("BLE connected");
  }

  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    connected = false;
    Serial.println("BLE disconnected, restarting advertising");
    NimBLEDevice::startAdvertising();
  }

  void begin(const char* deviceName = "Haptic Knob") {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this);

    hid = new NimBLEHIDDevice(pServer);
    hid->setManufacturer("DIY");
    hid->setPnp(0x02, 0x045E, 0x07A5, 0x0111);
    hid->setHidInfo(0x00, 0x01);

    hid->setReportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));

    // Keyboard input report (ID 1)
    input = hid->getInputReport(1);
    // Consumer input report (ID 2)
    consumer = hid->getInputReport(2);

    hid->startServices();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    adv->start();

    Serial.println("BLE HID advertising started");
  }

  bool isConnected() { return connected; }

  // Send keyboard report: modifier byte + up to 6 keycodes
  void sendKeyReport(uint8_t modifiers, uint8_t key1 = 0, uint8_t key2 = 0) {
    if (!connected) return;
    uint8_t report[8] = {modifiers, 0, key1, key2, 0, 0, 0, 0};
    input->setValue(report, sizeof(report));
    input->notify();
    delay(5);
    // Release
    memset(report, 0, sizeof(report));
    input->setValue(report, sizeof(report));
    input->notify();
  }

  // Send consumer control key
  void sendConsumer(uint16_t key) {
    if (!connected) return;
    uint8_t report[2] = {(uint8_t)(key & 0xFF), (uint8_t)(key >> 8)};
    consumer->setValue(report, sizeof(report));
    consumer->notify();
    delay(5);
    // Release
    uint8_t release[2] = {0, 0};
    consumer->setValue(release, sizeof(release));
    consumer->notify();
  }

  void volumeUp() {
    if (!connected) return;
    uint8_t report[8] = {MOD_LEFT_SHIFT | MOD_LEFT_ALT, 0, 0, 0, 0, 0, 0, 0};
    input->setValue(report, sizeof(report));
    input->notify();
    delay(5);
    sendConsumer(KEY_MEDIA_VOLUME_UP);
    memset(report, 0, sizeof(report));
    input->setValue(report, sizeof(report));
    input->notify();
  }

  void volumeDown() {
    if (!connected) return;
    uint8_t report[8] = {MOD_LEFT_SHIFT | MOD_LEFT_ALT, 0, 0, 0, 0, 0, 0, 0};
    input->setValue(report, sizeof(report));
    input->notify();
    delay(5);
    sendConsumer(KEY_MEDIA_VOLUME_DOWN);
    memset(report, 0, sizeof(report));
    input->setValue(report, sizeof(report));
    input->notify();
  }

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
};
