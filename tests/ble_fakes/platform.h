#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>
using TickType_t = uint32_t;
using UBaseType_t = unsigned;
constexpr int pdPASS = 1, pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE = 65535;
constexpr int BLE_SM_PAIR_AUTHREQ_BOND = 1, HID_KEYBOARD = 961;
static uint32_t testNow = 0;
uint32_t millis() { return testNow; }
uint32_t pdMS_TO_TICKS(uint32_t n) { return n; }
void vTaskDelay(uint32_t n) { testNow += n; }
struct Queue {
  size_t capacity, size;
  std::deque<std::vector<uint8_t>> data;
};
using QueueHandle_t = Queue *;
QueueHandle_t xQueueCreate(size_t n, size_t s) { return new Queue{n, s, {}}; }
int xQueueSend(QueueHandle_t q, const void *p, int) {
  if (q->data.size() == q->capacity)
    return 0;
  auto b = (const uint8_t *)p;
  q->data.emplace_back(b, b + q->size);
  return 1;
}
int xQueueSendToFront(QueueHandle_t q, const void *p, int) {
  if (q->data.size() == q->capacity)
    return 0;
  auto b = (const uint8_t *)p;
  q->data.emplace_front(b, b + q->size);
  return 1;
}
int xQueueReceive(QueueHandle_t q, void *p, uint32_t) {
  if (q->data.empty())
    return 0;
  memcpy(p, q->data.front().data(), q->size);
  q->data.pop_front();
  return 1;
}
void xQueueReset(QueueHandle_t q) { q->data.clear(); }
size_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->data.size(); }
int xTaskCreatePinnedToCore(void (*)(void *), const char *, int, void *, int, void *, int) {
  return 1;
}
struct SerialStub {
  int writes = 0;
  template <class... A> void printf(const char *, A...) { writes++; }
  void println(const char *) { writes++; }
} Serial;
struct ESPStub {
  unsigned getFreeHeap() { return 180000; }
} ESP;
struct NimBLEConnInfo {
  uint16_t handle = 0;
  bool encrypted = true;
  uint16_t getConnHandle() const { return handle; }
  uint16_t getConnInterval() const { return 12; }
  uint16_t getConnLatency() const { return 22; }
  uint16_t getConnTimeout() const { return 200; }
  bool isEncrypted() const { return encrypted; }
  bool isBonded() const { return encrypted; }
  bool isAuthenticated() const { return false; }
};
struct NimBLEServer;
struct NimBLECharacteristic;
struct NimBLEServerCallbacks {
  virtual void onConnect(NimBLEServer *, NimBLEConnInfo &) {}
  virtual void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) {}
  virtual void onAuthenticationComplete(NimBLEConnInfo &) {}
  virtual void onConnParamsUpdate(NimBLEConnInfo &) {}
  virtual void onPhyUpdate(NimBLEConnInfo &, uint8_t, uint8_t) {}
};
struct NimBLECharacteristicCallbacks {
  virtual void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t) {}
  virtual void onStatus(NimBLECharacteristic *, NimBLEConnInfo &, int) {}
};
static bool testNotifyAccepted = true;
struct SentReport { const void* chr; std::vector<uint8_t> data; };
static std::vector<SentReport> testReports;
static bool testFailConsumerRelease = false;
struct NimBLECharacteristic {
  void setCallbacks(NimBLECharacteristicCallbacks *) {}
  void setValue(const uint8_t *, size_t) {}
  bool notify(const uint8_t *data, size_t len, uint16_t) {
    if (!testNotifyAccepted || (testFailConsumerRelease && len == 2 && data[0] == 0 && data[1] == 0)) return false;
    testReports.push_back({this, {data, data + len}});
    return true;
  }
};
struct NimBLEServer {
  int disconnects = 0, updates = 0;
  void setCallbacks(NimBLEServerCallbacks *) {}
  void advertiseOnDisconnect(bool) {}
  bool start() { return true; }
  uint8_t getConnectedCount() { return 0; }
  bool disconnect(uint16_t) {
    disconnects++;
    return true;
  }
  void updateConnParams(uint16_t, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
    assert(a == 12 && b == 24 && c == 0 && d == 600);
    updates++;
  }
};
struct NimBLEService {
  int getUUID() { return 0x1812; }
};
struct NimBLEHIDDevice {
  NimBLECharacteristic keyboard, consumer;
  NimBLEService service;
  NimBLEHIDDevice(NimBLEServer *) {}
  void setManufacturer(const char *) {}
  void setPnp(int, int, int, int) {}
  void setHidInfo(int, int) {}
  void setReportMap(uint8_t *, size_t) {}
  NimBLECharacteristic *getInputReport(int n) { return n == 1 ? &keyboard : &consumer; }
  void startServices() {}
  NimBLEService *getHidService() { return &service; }
};
struct NimBLEAdvertising {
  bool active = false;
  int starts = 0;
  void setAppearance(int) {}
  void addServiceUUID(int) {}
  void enableScanResponse(bool) {}
  void setName(const char *) {}
  void setMinInterval(int) {}
  void setMaxInterval(int) {}
  bool start() {
    active = true;
    starts++;
    return true;
  }
  bool isAdvertising() { return active; }
};
struct NimBLEDevice {
  static bool init(const char *) { return true; }
  static void setSecurityAuth(int) {}
  static void setPower(int) {}
  static NimBLEServer *createServer() { return new NimBLEServer; }
  static NimBLEAdvertising *getAdvertising() {
    static NimBLEAdvertising adv;
    return &adv;
  }
  static bool startSecurity(uint16_t) { return true; }
};
struct NimBLEUtils {
  static const char *returnCodeToString(int) { return "test"; }
};

constexpr int BLE_HS_ENOTCONN = 7;
static bool testLinkPresent = true;
struct ble_gap_conn_desc {};
int ble_gap_conn_find(uint16_t, ble_gap_conn_desc *) {
  return testLinkPresent ? 0 : BLE_HS_ENOTCONN;
}
