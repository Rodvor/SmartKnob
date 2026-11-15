#pragma once

#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#if __has_include(<NimBLECppVersion.h>)
#include <NimBLECppVersion.h>
#define SMARTKNOB_HAS_NIMBLE_VERSION_HEADER 1
#endif
#include <HIDTypes.h>
#include <cstdarg>
#include <atomic>
#include <mutex>
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

// Each action owns its press and release, including fine-volume modifiers.
static constexpr uint32_t HID_PHASE_MS = 30;
static constexpr uint32_t HID_ACTION_MAX_AGE_MS = 250;
static constexpr uint32_t BLE_DIAGNOSTIC_INTERVAL_MS = 15000;
static constexpr uint32_t BLE_NOTIFY_RETRY_TIMEOUT_MS = 250;
static constexpr uint32_t BLE_LINK_CHECK_INTERVAL_MS = 1000;
static constexpr uint32_t BLE_HID_SETUP_TIMEOUT_MS = 15000;
static constexpr uint32_t BLE_PARAM_REQUEST_DELAY_MS = 5000;

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

  // Callbacks run on the Bluetooth host task. Never write flash or wait for
  // Serial here: queue diagnostics in RAM and drain them from poll().
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    std::lock_guard<std::mutex> lock(connectionMutex);
    adoptConnection(info);
    printConnectionInfo("connected", info);
    char buf[160];
    snprintf(buf, sizeof(buf),
             "Connected handle=%u interval=%.2fms latency=%u timeout=%ums bonded=%u encrypted=%u",
             info.getConnHandle(), info.getConnInterval() * 1.25f,
             info.getConnLatency(), info.getConnTimeout() * 10,
             info.isBonded(), info.isEncrypted());
    logEvent(buf);
    // Parameter negotiation is deferred until pairing and subscriptions settle.
  }

  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    std::lock_guard<std::mutex> lock(connectionMutex);
    if (connected.load() && info.getConnHandle() != connHandle.load()) return;
    const uint32_t duration = (millis() - connectedSince.load()) / 1000;
    clearConnection();
    char buf[240];
    snprintf(buf, sizeof(buf),
             "Disconnected reason=0x%03X (%s), duration=%lus, queued=%lu sent=%lu "
             "notify_fail=%lu status_err=%lu dropped=%lu suppressed=%lu heap=%u",
             reason, disconnectReasonText(reason), (unsigned long)duration,
             (unsigned long)reportsQueued.load(), (unsigned long)reportsSent.load(),
             (unsigned long)notifyFailures.load(), (unsigned long)notifyStatusErrors.load(),
             (unsigned long)queueDrops.load(), (unsigned long)reportsSuppressed.load(),
             ESP.getFreeHeap());
    logEvent(buf);
  }

  void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& info, uint16_t subValue) override {
    std::lock_guard<std::mutex> lock(connectionMutex);
    const bool notificationsEnabled = (subValue & 0x01) != 0;
    // Bond restoration can precede onConnect(), or a late connection event can
    // be lost after a rapid reconnect. These callbacks also identify the link.
    if (notificationsEnabled) adoptConnection(info);
    if (connected.load() && info.getConnHandle() != connHandle.load()) return;
    const char* reportName = "unknown";
    if (chr == input) {
      reportName = "keyboard";
      keyboardSubscribed.store(notificationsEnabled);
      if (notificationsEnabled) enqueueRelease(input, 8);
    } else if (chr == consumer) {
      reportName = "consumer";
      consumerSubscribed.store(notificationsEnabled);
      if (notificationsEnabled) enqueueRelease(consumer, 2);
    }
    char buf[160];
    snprintf(buf, sizeof(buf),
             "Subscription report=%s value=%u notify=%u handle=%u bonded=%u encrypted=%u",
             reportName, subValue, notificationsEnabled, info.getConnHandle(),
             info.isBonded(), info.isEncrypted());
    logEvent(buf);
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    std::lock_guard<std::mutex> lock(connectionMutex);
    adoptConnection(info);
    if (info.isEncrypted()) {
      if (keyboardSubscribed.load()) enqueueRelease(input, 8);
      if (consumerSubscribed.load()) enqueueRelease(consumer, 2);
    }
    printConnectionInfo("authenticated", info);
  }

  void onConnParamsUpdate(NimBLEConnInfo& info) override {
    printConnectionInfo("params updated", info);
  }

  void onPhyUpdate(NimBLEConnInfo& info, uint8_t txPhy, uint8_t rxPhy) override {
    logMessage("[BLE] PHY updated handle=%u tx=%u rx=%u",
                  info.getConnHandle(), txPhy, rxPhy);
  }

  void onStatus(NimBLECharacteristic* chr, NimBLEConnInfo& info, int code) override {
    notifyStatuses.fetch_add(1);
    if (code == 0) return;
    notifyStatusErrors.fetch_add(1);
    if (connected.load() && info.getConnHandle() == connHandle.load())
      recoveryGeneration.store(connectionGeneration.load());
    logMessage("[BLE] notify status error report=%s handle=%u code=%d (%s)",
                  reportName(chr), info.getConnHandle(), code,
                  NimBLEUtils::returnCodeToString(code));
  }

  bool begin(const char* deviceName = "Haptic Knob", void (*addServices)(NimBLEServer*) = nullptr) {
    logQueue = xQueueCreate(24, sizeof(LogEntry));
    reportQueue = xQueueCreate(8, sizeof(HidAction));
    if (!logQueue || !reportQueue) return false;
    logEvent("Device booted: input and release recovery build 2026-11-03");

    if (!NimBLEDevice::init(deviceName)) return false;
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);
    NimBLEDevice::setPower(9); // NimBLE 2.x expects dBm, not esp_power_level_t.
#ifdef SMARTKNOB_HAS_NIMBLE_VERSION_HEADER
    Serial.printf("[BLE] stack=%s arduino-esp32=%s\n",
                  NimBLEDevice::getVersion(), ESP_ARDUINO_VERSION_STR);
#endif

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this);
    pServer->advertiseOnDisconnect(true);

    hid = new NimBLEHIDDevice(pServer);
    hid->setManufacturer("Hugo Minkkinen");
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

    if (addServices) addServices(pServer);

    // NimBLE 2.4+ starts all registered services through the server. Older
    // versions require the HID-specific startServices() call instead.
#ifdef SMARTKNOB_HAS_NIMBLE_VERSION_HEADER
    if (!pServer->start()) return false;
#else
    hid->startServices();
#endif

    lastReportTaskActivity.store(millis());
    if (xTaskCreatePinnedToCore(reportTask, "hid_report", 4096, this, 2,
                              nullptr, 0) != pdPASS) return false;

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->getHidService()->getUUID());
    adv->enableScanResponse(true);
    adv->setName(deviceName);
    adv->setMinInterval(32); // 20 ms, also used when reconnecting.
    adv->setMaxInterval(32);
    logEvent(adv->start() ? "Advertising started" : "Advertising start failed; will retry");
    return true;
  }

  // Cancels unstarted actions only; the worker still releases its active press.
  void cancelPendingInputs() {
    if (reportQueue) xQueueReset(reportQueue);
  }

  std::atomic<bool> companionActive{false};

  bool isConnected() const { return connected.load(); }

  bool isReady() const {
    return connected.load() && encrypted.load() && keyboardSubscribed.load() &&
           consumerSubscribed.load() && recoveryGeneration.load() != connectionGeneration.load();
  }

  void poll() {
    drainLogs();
    const uint32_t now = millis();
    maintainConnection(now);
    if (now - lastDiagnosticPrint < BLE_DIAGNOSTIC_INTERVAL_MS) return;
    lastDiagnosticPrint = now;

    const UBaseType_t queued = reportQueue ? uxQueueMessagesWaiting(reportQueue) : 0;
    const uint32_t workerAge = now - lastReportTaskActivity.load();
    Serial.printf(
        "[BLE health] connected=%u encrypted=%u ready=%u advertising=%u "
        "keyboard=%u consumer=%u handle=%u generation=%lu "
        "queue=%u queued=%lu sent=%lu notify_fail=%lu statuses=%lu status_err=%lu "
        "dropped=%lu suppressed=%lu worker_age=%lums worker_stalled=%u log_drops=%lu heap=%u\n",
        connected.load(), encrypted.load(), isReady(), NimBLEDevice::getAdvertising()->isAdvertising(),
        keyboardSubscribed.load(), consumerSubscribed.load(),
        connHandle.load(), (unsigned long)connectionGeneration.load(),
        (unsigned int)queued, (unsigned long)reportsQueued.load(),
        (unsigned long)reportsSent.load(), (unsigned long)notifyFailures.load(),
        (unsigned long)notifyStatuses.load(), (unsigned long)notifyStatusErrors.load(),
        (unsigned long)queueDrops.load(), (unsigned long)reportsSuppressed.load(),
        (unsigned long)workerAge, queued > 0 && workerAge > 1000,
        (unsigned long)logDrops.load(), ESP.getFreeHeap());
  }

  // Send keyboard report: modifier byte + up to 6 keycodes
  void sendKeyReport(uint8_t modifiers, uint8_t key1 = 0, uint8_t key2 = 0) {
    if (!isReady() || !reportQueue) {
      reportsSuppressed.fetch_add(1);
      return;
    }
    uint8_t report[8] = {modifiers, 0, key1, key2, 0, 0, 0, 0};
    enqueueAction(input, report, sizeof(report));
  }

  // Send consumer control key
  void sendConsumer(uint16_t key) {
    if (!isReady() || !reportQueue) {
      reportsSuppressed.fetch_add(1);
      return;
    }
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

  struct LogEntry {
    uint32_t at;
    char message[256];
  };

  // Serialize callback state with the periodic check of NimBLE's actual links.
  // Held only for short RAM updates and connection lookups, never for I/O.
  std::mutex connectionMutex;
  std::atomic<bool> connected{false};
  std::atomic<bool> encrypted{false};
  std::atomic<uint32_t> connectedSince{0};
  std::atomic<uint32_t> recoveryGeneration{UINT32_MAX};
  std::atomic<uint32_t> logDrops{0};
  QueueHandle_t logQueue = nullptr;
  uint32_t lastLinkCheck = 0;
  uint32_t paramsRequestedGeneration = UINT32_MAX;
  uint32_t securityRequestedGeneration = UINT32_MAX;
  std::atomic<bool> keyboardSubscribed{false};
  std::atomic<bool> consumerSubscribed{false};
  std::atomic<uint16_t> connHandle{BLE_HS_CONN_HANDLE_NONE};
  std::atomic<uint32_t> connectionGeneration{0};
  std::atomic<uint32_t> reportsQueued{0};
  std::atomic<uint32_t> reportsSent{0};
  std::atomic<uint32_t> notifyFailures{0};
  std::atomic<uint32_t> notifyStatuses{0};
  std::atomic<uint32_t> notifyStatusErrors{0};
  std::atomic<uint32_t> queueDrops{0};
  std::atomic<uint32_t> reportsSuppressed{0};
  std::atomic<uint32_t> lastReportTaskActivity{0};
  uint32_t lastDiagnosticPrint = 0;
  QueueHandle_t reportQueue = nullptr;

  void logEvent(const char* message) {
    if (!logQueue) return;
    LogEntry entry{};
    entry.at = millis();
    snprintf(entry.message, sizeof(entry.message), "%s", message);
    if (xQueueSend(logQueue, &entry, 0) != pdPASS) logDrops.fetch_add(1);
  }

  void logMessage(const char* format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    logEvent(message);
  }

  void drainLogs() {
    LogEntry entry;
    // Bound the work so a burst of diagnostics cannot starve the UI or recovery.
    for (int i = 0; i < 4 && xQueueReceive(logQueue, &entry, 0) == pdTRUE; ++i) {
      Serial.printf("[%lums] %s\n", (unsigned long)entry.at, entry.message);
    }
  }

  void adoptConnection(const NimBLEConnInfo& info) {
    if (!connected.load() || connHandle.load() != info.getConnHandle()) {
      connected.store(false);
      keyboardSubscribed.store(false);
      consumerSubscribed.store(false);
      connectionGeneration.fetch_add(1);
      if (reportQueue) xQueueReset(reportQueue);
      connHandle.store(info.getConnHandle());
      connectedSince.store(millis());
      encrypted.store(info.isEncrypted());
      connected.store(true);
    } else {
      // Do not clear restored subscriptions or invalidate queued reports when
      // onConnect arrives after authentication/subscription callbacks.
      encrypted.store(info.isEncrypted());
    }
  }

  void clearConnection() {
    connected.store(false);
    encrypted.store(false);
    keyboardSubscribed.store(false);
    consumerSubscribed.store(false);
    connHandle.store(BLE_HS_CONN_HANDLE_NONE);
    connectionGeneration.fetch_add(1);
    if (reportQueue) xQueueReset(reportQueue);
  }

  void maintainConnection(uint32_t now) {
    if (now - lastLinkCheck < BLE_LINK_CHECK_INTERVAL_MS) return;
    lastLinkCheck = now;
    {
      std::lock_guard<std::mutex> lock(connectionMutex);
      ble_gap_conn_desc desc{};
      if (connected.load() && ble_gap_conn_find(connHandle.load(), &desc) == BLE_HS_ENOTCONN) {
        logEvent("Connection disappeared from stack; clearing stale HID state");
        clearConnection();
      }
    }
    if (!connected.load()) {
      auto* adv = NimBLEDevice::getAdvertising();
      if (!adv->isAdvertising() && pServer->getConnectedCount() == 0) {
        logEvent(adv->start() ? "Advertising recovered" : "Advertising retry failed");
      }
      return;
    }

    const uint16_t handle = connHandle.load();
    const uint32_t generation = connectionGeneration.load();
    const uint32_t age = now - connectedSince.load();
    if (generation == recoveryGeneration.load() ||
        (!isReady() && !companionActive.load() && age >= BLE_HID_SETUP_TIMEOUT_MS)) {
      // A link without usable HID reports must not stay silently connected.
      // Keep the bond; let macOS reconnect and restore encryption/subscriptions.
      recoveryGeneration.store(generation);
      logMessage("Recovering incomplete HID link handle=%u age=%lums", handle,
                 (unsigned long)age);
      pServer->disconnect(handle);
      return;
    }

    if (!encrypted.load() && age >= 1000 && securityRequestedGeneration != generation) {
      securityRequestedGeneration = generation;
      logEvent(NimBLEDevice::startSecurity(handle) ? "Security requested" : "Security request failed");
    }

    if (isReady() && age >= BLE_PARAM_REQUEST_DELAY_MS &&
        paramsRequestedGeneration != generation) {
      paramsRequestedGeneration = generation;
      // One request per link, outside the callback, after the Mac's own initial
      // update. 15-30 ms, zero skipped events, 6 s timeout follow Apple QA1931.
      // The host may decline; do not keep renegotiating if it does.
      logEvent("Requesting connection interval=15-30ms latency=0 timeout=6000ms");
      pServer->updateConnParams(handle, 12, 24, 0, 600);
    }
  }

  void releaseOrRecover(const HidAction& action, NimBLECharacteristic* chr,
                        const uint8_t* data, uint8_t len) {
    if (!notifyWhenReady(action, chr, data, len, true) && connected.load() &&
        action.generation == connectionGeneration.load()) {
      recoveryGeneration.store(action.generation);
      logEvent("Key release failed; reconnect required before sending more input");
    }
  }

  void processAction(const HidAction& action) {
    if (!actionIsCurrent(action)) return;
    // Expire only unstarted presses. A release must never expire or compete
    // for queue space with new input.
    if (action.releaseAfter && millis() - action.queuedAt > HID_ACTION_MAX_AGE_MS) {
      queueDrops.fetch_add(1);
      return;
    }
    uint8_t release[8] = {};
    if (!action.releaseAfter) {
      releaseOrRecover(action, action.chr, release, action.len);
      return;
    }
    if (action.modifiers) {
      uint8_t modifiers[8] = {action.modifiers, 0, 0, 0, 0, 0, 0, 0};
      if (!notifyWhenReady(action, input, modifiers, sizeof(modifiers))) {
        releaseOrRecover(action, input, release, 8);
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(HID_PHASE_MS));
    }
    if (notifyWhenReady(action, action.chr, action.data, action.len))
      vTaskDelay(pdMS_TO_TICKS(HID_PHASE_MS));
    // Even an uncertain press gets cleanup. Recovery blocks new presses but
    // still permits releases on this connection.
    releaseOrRecover(action, action.chr, release, action.len);
    vTaskDelay(pdMS_TO_TICKS(HID_PHASE_MS));
    if (action.modifiers) {
      releaseOrRecover(action, input, release, 8);
      vTaskDelay(pdMS_TO_TICKS(HID_PHASE_MS));
    }
  }

  static void reportTask(void* param) {
    auto* self = static_cast<NimBLEKeyboard*>(param);
    HidAction action;
    while (true) {
      if (xQueueReceive(self->reportQueue, &action, portMAX_DELAY) == pdTRUE) {
        self->lastReportTaskActivity.store(millis());
        self->processAction(action);
        self->lastReportTaskActivity.store(millis());
      }
    }
  }

  bool reportIsReady(const HidAction& action, NimBLECharacteristic* chr, bool releasing = false) const {
    if (!connected.load() || !encrypted.load() ||
        action.generation != connectionGeneration.load() ||
        (!releasing && action.generation == recoveryGeneration.load())) return false;
    if (chr == input) return keyboardSubscribed.load();
    if (chr == consumer) return consumerSubscribed.load();
    return false;
  }

  bool actionIsCurrent(const HidAction& action) const {
    if (!reportIsReady(action, action.chr)) return false;
    return action.modifiers == 0 || reportIsReady(action, input);
  }

  bool notifyWhenReady(const HidAction& action, NimBLECharacteristic* chr,
                       const uint8_t* data, uint8_t len, bool releasing = false) {
    const uint32_t startedAt = millis();
    uint32_t attempts = 0;
    while (reportIsReady(action, chr, releasing) &&
           millis() - startedAt < BLE_NOTIFY_RETRY_TIMEOUT_MS) {
      attempts++;
      const uint16_t handle = connHandle.load();
      if (chr->notify(data, len, handle)) {
        reportsSent.fetch_add(1);
        return true;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    notifyFailures.fetch_add(1);
    logMessage("[BLE] notify rejected report=%s attempts=%lu elapsed=%lums "
                  "connected=%u keyboard=%u consumer=%u generation=%lu/%lu",
                  reportName(chr), (unsigned long)attempts,
                  (unsigned long)(millis() - startedAt), connected.load(),
                  keyboardSubscribed.load(), consumerSubscribed.load(),
                  (unsigned long)action.generation,
                  (unsigned long)connectionGeneration.load());
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
    if (xQueueSend(reportQueue, &action, 0) == pdPASS) {
      reportsQueued.fetch_add(1);
    } else {
      queueDrops.fetch_add(1);
      logMessage("[BLE] report queue full report=%s", reportName(chr));
    }
  }

  void enqueueRelease(NimBLECharacteristic* chr, uint8_t len) {
    if (!connected.load() || !reportQueue) return;
    HidAction action{};
    action.chr = chr;
    action.len = len;
    action.generation = connectionGeneration.load();
    action.releaseAfter = false;
    if (xQueueSendToFront(reportQueue, &action, 0) == pdPASS) {
      reportsQueued.fetch_add(1);
    } else {
      queueDrops.fetch_add(1);
      logMessage("[BLE] release queue full report=%s", reportName(chr));
      recoveryGeneration.store(action.generation);
    }
  }

  void sendVolumeKey(uint16_t key) {
    if (!isReady() || !reportQueue) {
      reportsSuppressed.fetch_add(1);
      return;
    }
    uint8_t report[2] = {(uint8_t)(key & 0xFF), (uint8_t)(key >> 8)};
    enqueueAction(consumer, report, sizeof(report), MOD_LEFT_SHIFT | MOD_LEFT_ALT);
  }

  const char* reportName(NimBLECharacteristic* chr) const {
    if (chr == input) return "keyboard";
    if (chr == consumer) return "consumer";
    return "unknown";
  }

  static const char* disconnectReasonText(int reason) {
    // NimBLE's full return-code text table is optional at compile time.
    switch (reason) {
      case 0x208: return "connection supervision timeout";
      case 0x213: return "remote device ended connection";
      case 0x216: return "local host ended connection";
      default: {
        const char* text = NimBLEUtils::returnCodeToString(reason);
        return text && text[0] ? text : "unknown";
      }
    }
  }

  void printConnectionInfo(const char* event, const NimBLEConnInfo& info) {
    logMessage("[BLE] %s handle=%u interval=%.2fms latency=%u timeout=%ums "
                  "bonded=%u encrypted=%u authenticated=%u heap=%u",
                  event, info.getConnHandle(), info.getConnInterval() * 1.25f,
                  info.getConnLatency(), info.getConnTimeout() * 10,
                  info.isBonded(), info.isEncrypted(), info.isAuthenticated(),
                  ESP.getFreeHeap());
  }
};
