// Note: Colors for fonts using RGB565

// Include libraries
#include <TFT_eSPI.h>
#include <Wire.h>
#include <SimpleFOC.h>
#include <math.h>
#include "NimbleKeyboard.h"
#include "InputFilter.h"
#include "TouchFilter.h"

// Include configurations and icons
#include "menu_config.h"
#include "pin_config.h"
#include "icons.h"

#define CST816S_ADDR        0x15
#define CST816S_FINGER_REG  0x02
#define CST816S_X_HIGH_REG  0x03
#define CST816S_X_LOW_REG   0x04
#define CST816S_SWIPE_THRESHOLD 40

// Minimum torque. No torque if below this number
#define MINIMUM_TORQUE  0.05

// Moving ball sprite

#define BALL_RADIUS       5
#define BALL_SPRITE_SIZE  40   // 2*(BALL_RADIUS+margin) + headroom for movement per frame
#define SCREEN_SIZE       240  // adjust if your panel isn't 240x240
#define SPRITE_TRANSPARENT 0xF81F  // magenta marker, unused elsewhere in the UI

// Define objects
MagneticSensorI2C encoder = MagneticSensorI2C(AS5600_I2C);
TFT_eSPI tft = TFT_eSPI();
BLDCMotor motor = BLDCMotor(7);
BLDCDriver3PWM driver = BLDCDriver3PWM(IN1, IN2, IN3, EN);
NimBLEKeyboard bleKeyboard;

TFT_eSprite ballSprite = TFT_eSprite(&tft);

// For notches
const int cx = 120;
const int cy = 120;
const int radius = 120;
const int lineLength = 7;

// For motor calculations
float currentPos = 0;
float max_torque = 0.3;
float torque_build_up = 0.8;
int current_choice = 0;

// For UI logic
bool display_status = true;
bool take_input = false;
unsigned long last_activity = 0;
int current_ui_id = -1;
int new_ui_id = VOLUME_ID;
int n_lines = VOLUME_NOTCHES;
int discord_choice = 0;

const int CHOICES[] = {VOLUME_ID, MEDIA_ID, BRIGHT_ID, DISCORD_ID};
const int N_CHOICES = 4;
int current_menu_index = 0;

TouchFilter touchFilter;
TwoWire* touchBus = nullptr;
std::mutex sensorBusMutex;
uint32_t touchReadErrors = 0, touchReadSuccesses = 0, touchGestures = 0;

float sleep_pos = 0;
#define WAKE_ROTATION_THRESHOLD (PI / 60.0)

TaskHandle_t focTask;
std::atomic<float> sampledAngle{0};
std::atomic<float> requestedTorque{0};
DetentFilter detents;
unsigned long centeredSince = 0;

void focLoop(void* param) {
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(sensorBusMutex);
      motor.loopFOC();
      sampledAngle.store(encoder.getAngle());
      motor.move(requestedTorque.load());
    }
    vTaskDelay(1);
  }
}

bool probeTouch(TwoWire& bus, uint8_t& chip) {
  bus.beginTransmission(CST816S_ADDR);
  bus.write(0xA7); // CST816 chip ID
  if (bus.endTransmission() != 0 || bus.requestFrom(CST816S_ADDR, (uint8_t)1) != 1)
    return false;
  chip = bus.read();
  return true;
}

void initTouch() {
  uint8_t chip = 0;
  // Earlier working firmware read touch on Wire. Detect the actual wiring
  // rather than assuming the dedicated pin definitions match this device.
  if (probeTouch(Wire, chip)) touchBus = &Wire;
  else if (probeTouch(Wire1, chip)) touchBus = &Wire1;
  if (!touchBus) {
    Serial.println("[Touch] Controller not found on either bus");
    return;
  }
  // This firmware polls coordinates, so keep the CST816 awake (0xFE).
  touchBus->beginTransmission(CST816S_ADDR);
  touchBus->write(0xFE);
  touchBus->write(0xFF);
  bool awake = touchBus->endTransmission() == 0;
  Serial.printf("[Touch] bus=%s chip=0x%02X polling_awake=%u\n",
                touchBus == &Wire ? "Wire (21/22)" : "Wire1 (19/18)", chip, awake);
}

bool readTouchSample(uint8_t& fingers, int& x) {
  if (!touchBus) return false;
  // The controller may share Wire with the motor encoder. Protect the whole
  // register-read transaction, not just each individual Wire API call.
  std::unique_lock<std::mutex> lock(sensorBusMutex, std::defer_lock);
  if (touchBus == &Wire) lock.lock();
  touchBus->beginTransmission(CST816S_ADDR);
  touchBus->write(CST816S_FINGER_REG);
  if (touchBus->endTransmission() != 0) return false;
  if (touchBus->requestFrom(CST816S_ADDR, (uint8_t)3) != 3) return false;
  fingers = touchBus->read();
  int hi = touchBus->read();
  int lo = touchBus->read();
  x = ((hi & 0x0F) << 8) | lo;
  return fingers <= 1 && (fingers == 0 || x < SCREEN_SIZE);
}

void setup() {
  Serial.begin(115200);
  delay(500); // give Serial monitor time to connect
  if (!bleKeyboard.begin("Haptic Knob")) {
    Serial.println("BLE initialization failed");
    while (true) delay(1000);
  }

  // Set blacklight pins
  pinMode(BLK_PIN, OUTPUT);
  digitalWrite(BLK_PIN, HIGH);

  // Init screen
  tft.init();
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xad75);
  // Ball sprite
  ballSprite.setColorDepth(16);
  ballSprite.createSprite(BALL_SPRITE_SIZE, BALL_SPRITE_SIZE);

  // Bluetooth connecting screen
  tft.fillScreen(TFT_BLACK);
  tft.drawBitmap(ICON_X, ICON_Y, icon_bluetooth, ICON_W, ICON_H, BLUETOOTH_COLOR);
  tft.drawString("Connecting...", 122, 180);


  Wire.begin(ENCODER_SDA, ENCODER_SCL);
  encoder.init();

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(20);
  digitalWrite(TP_RST, HIGH);
  delay(100);
  pinMode(TP_INT, INPUT);
  Wire1.begin(TP_SDA, TP_SCL);
  initTouch();

  driver.voltage_power_supply = 12.0;
  driver.init();
  motor.linkDriver(&driver);
  motor.linkSensor(&encoder);
  motor.controller = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  motor.voltage_limit = 3.0;
  motor.init();
  motor.initFOC();

  sampledAngle.store(encoder.getAngle());
  xTaskCreatePinnedToCore(focLoop, "FOC", 4096, NULL, 1, &focTask, 1);

  while (!bleKeyboard.isReady()) {
    bleKeyboard.poll();
    delay(10);
  }

  Serial.println("BLE connected, starting UI");
}

void doTapAction() {
  if (!bleKeyboard.isConnected()) return;
  if      (current_ui_id == VOLUME_ID)  bleKeyboard.playPause();
  else if (current_ui_id == MEDIA_ID)   bleKeyboard.playPause();
  else if (current_ui_id == DISCORD_ID) bleKeyboard.pressShiftAltF13();
  // BRIGHT_ID: no tap action
}

void swipeToMenu(int direction) {
  current_menu_index = (current_menu_index + direction + N_CHOICES) % N_CHOICES;
  new_ui_id = CHOICES[current_menu_index];
  last_activity = millis();
}

void handleTouchRevised() {
  const uint32_t now = millis();
  uint8_t fingers = 0;
  int x = 0;
  const bool valid = readTouchSample(fingers, x);
  if (valid) ++touchReadSuccesses;
  else ++touchReadErrors;
  auto gesture = touchFilter.update(valid, fingers, x, now);
  static uint32_t lastDiagnostic = 0;
  if (now - lastDiagnostic >= 15000) {
    lastDiagnostic = now;
    Serial.printf("[Touch] reads=%lu errors=%lu gestures=%lu\n",
                  (unsigned long)touchReadSuccesses, (unsigned long)touchReadErrors,
                  (unsigned long)touchGestures);
  }
  if (gesture == TouchFilter::None) return;
  ++touchGestures;
  Serial.printf("[Touch] gesture=%u\n", static_cast<unsigned>(gesture));
  if (!display_status) {
    setDisplay(true);
    last_activity = now;
    return;
  }
  if (gesture == TouchFilter::SwipeRight) swipeToMenu(+1);
  else if (gesture == TouchFilter::SwipeLeft) swipeToMenu(-1);
  else { doTapAction(); last_activity = now; }
}

void loop() {
  bleKeyboard.poll();
  handleTouchRevised();

  if (new_ui_id != current_ui_id) {
    bleKeyboard.cancelPendingInputs();
    drawUI(new_ui_id);
    take_input = false;
    centeredSince = 0;
  }

  float pos = sampledAngle.load();
  int choice = calc_choice(pos);

  if (take_input) {
    requestedTorque.store(calc_torque(pos));
  } else {
    float target_angle = 0;
    if (current_ui_id == DISCORD_ID) target_angle = discord_choice * 2 * PI / 3;
    requestedTorque.store(torque_to_angle(pos, target_angle));
    if (abs(((target_angle - PI / 2) - pos)) < 0.05) {
      if (centeredSince == 0) centeredSince = millis();
      if (millis() - centeredSince >= 150) {
        take_input = true;
        detents.reset(pos, n_lines);
        current_choice = choice;
      }
    } else centeredSince = 0;
  }

  if (pos != currentPos) {

    if (!display_status && fabs(pos - sleep_pos) > WAKE_ROTATION_THRESHOLD) {
      setDisplay(true);
      last_activity = millis();
    }

    drawBall(currentPos, pos);

    int direction = take_input ? detents.update(pos) : 0;
    int new_choice = detents.choice();

    if (direction != 0 && take_input) {
      if (direction > 0) {
        if      (current_ui_id == VOLUME_ID)  bleKeyboard.volumeUp();
        else if (current_ui_id == MEDIA_ID)   bleKeyboard.nextTrack();
        else if (current_ui_id == BRIGHT_ID)  bleKeyboard.pressF15();
      } else {
        if      (current_ui_id == VOLUME_ID)  bleKeyboard.volumeDown();
        else if (current_ui_id == MEDIA_ID)   bleKeyboard.prevTrack();
        else if (current_ui_id == BRIGHT_ID)  bleKeyboard.pressF14();
      }

      if (current_ui_id == DISCORD_ID) {
        if (new_choice == 0) {
          bleKeyboard.pressF13();
        } else if (new_choice == 1 && current_choice == 0) {
          bleKeyboard.pressF13();
        } else if (new_choice == 2 && current_choice == 0) {
          bleKeyboard.pressF13(); bleKeyboard.pressAltF13();
        } else {
          bleKeyboard.pressAltF13();
        }
        discord_choice = new_choice;
      }

      if (!display_status) setDisplay(true);
      last_activity = millis();
      current_choice = new_choice;
    }

    currentPos = pos;
  }

  if (display_status && millis() - last_activity > IDLE_TIMEOUT * 60000) {
    setDisplay(false);
  }

  vTaskDelay(1); // yield instead of blocking delay
}

// --- UI ---

void drawMenuIcon(int id) {
  const uint8_t* bmp = nullptr;
  unsigned int color = TFT_WHITE;
  switch (id) {
    case VOLUME_ID:  bmp = icon_volume;     color = VOLUME_COLOR;  break;
    case MEDIA_ID:   bmp = icon_media;      color = MEDIA_COLOR;   break;
    case DISCORD_ID: bmp = icon_discord;    color = DISCORD_COLOR; break;
    case BRIGHT_ID:  bmp = icon_brightness; color = BRIGHT_COLOR;  break;
  }
  if (bmp) {
    tft.drawBitmap(ICON_X, ICON_Y, bmp, ICON_W, ICON_H, color);
  }
}

void drawSmallIcon(int id, int x, int y) {
  const uint8_t* bmp = nullptr;
  unsigned int color = TFT_WHITE;
  switch (id) {
    case UNMUTE_ID:  bmp = icon_unmute;   color = UNMUTE_COLOR;  break;
    case MUTE_ID:   bmp = icon_mute;      color = MUTE_COLOR;   break;
    case DEAFEN_ID: bmp = icon_deafen;    color = DEAFEN_COLOR; break;
  }
  if (bmp) {
    tft.drawBitmap(x, y, bmp, SMALL_ICON_W, SMALL_ICON_H, color);
  }
}

void drawUI(int id) {
  tft.fillScreen(TFT_BLACK);
  current_ui_id = id;
  drawMenuIcon(id);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xad75);
  if      (id == VOLUME_ID)  { tft.drawString("Volume",     120, 180); max_torque = VOLUME_TORQUE;  torque_build_up = VOLUME_BUILDUP;  n_lines = VOLUME_NOTCHES; }
  else if (id == MEDIA_ID)   { tft.drawString("Media",      120, 180); max_torque = MEDIA_TORQUE;   torque_build_up = MEDIA_BUILDUP;   n_lines = MEDIA_NOTCHES; }
  else if (id == DISCORD_ID) { 
    tft.drawString("Discord", 120, 180);
    drawSmallIcon(UNMUTE_ID, 108, 30);
    drawSmallIcon(MUTE_ID, 178, 155);
    drawSmallIcon(DEAFEN_ID, 44, 155);
    max_torque = DISCORD_TORQUE; 
    torque_build_up = DISCORD_BUILDUP; 
    n_lines = DISCORD_NOTCHES; 
    }
  else if (id == BRIGHT_ID)  { tft.drawString("Brightness", 120, 180); max_torque = BRIGHT_TORQUE;  torque_build_up = BRIGHT_BUILDUP;  n_lines = BRIGHT_NOTCHES; }
  drawClockLines();
}

void drawClockLines() {
  for (int i = 0; i < n_lines; i++) {
    float angle = i * (2.0 * PI / n_lines) - PI / 2;
    int x0 = round(cx + radius * cos(angle));
    int y0 = round(cy + radius * sin(angle));
    int x1 = round(cx + (radius - lineLength) * cos(angle));
    int y1 = round(cy + (radius - lineLength) * sin(angle));
    tft.drawLine(x0, y0, x1, y1, 0x9492);
  }
}

float calc_torque(float pos) {
  const float amplitude = torque_build_up * max_torque;
  const float inside_tan = 0.5 * n_lines * (pos + PI / 2);
  float torque = amplitude * tan(inside_tan);

  if (abs(torque) < MINIMUM_TORQUE) return 0.0;
  if (torque > max_torque) return max_torque;
  if (torque < -max_torque) return -max_torque;
  return torque;
}

float torque_to_angle(float pos, float angle) {
  return 0.5 * cbrt(pos + PI / 2 - angle);
}

int calc_choice(float pos) {
  float angle = pos + PI / 2;
  while (angle < 0) angle += 2 * PI;
  while (angle >= 2 * PI) angle -= 2 * PI;
  return ((int)round(angle / (2.0 * PI) * n_lines)) % n_lines;
}

void drawBall(float posOld, float posNew) {
  int xOld = round(cx + (radius - 18) * cos(posOld));
  int yOld = round(cy + (radius - 18) * sin(posOld));
  int xNew = round(cx + (radius - 18) * cos(posNew));
  int yNew = round(cy + (radius - 18) * sin(posNew));

  const int margin = 2;
  int minX = min(xOld, xNew) - BALL_RADIUS - margin;
  int minY = min(yOld, yNew) - BALL_RADIUS - margin;
  int maxX = max(xOld, xNew) + BALL_RADIUS + margin;
  int maxY = max(yOld, yNew) + BALL_RADIUS + margin;
  int w = maxX - minX;
  int h = maxY - minY;

  if (w <= BALL_SPRITE_SIZE && h <= BALL_SPRITE_SIZE) {
    minX = constrain(minX, 0, SCREEN_SIZE - BALL_SPRITE_SIZE);
    minY = constrain(minY, 0, SCREEN_SIZE - BALL_SPRITE_SIZE);

    ballSprite.fillSprite(SPRITE_TRANSPARENT);
    ballSprite.fillCircle(xOld - minX, yOld - minY, BALL_RADIUS, TFT_BLACK);
    ballSprite.fillCircle(xNew - minX, yNew - minY, BALL_RADIUS, TFT_WHITE);
    ballSprite.pushSprite(minX, minY, SPRITE_TRANSPARENT);
  } else {
    tft.fillCircle(xOld, yOld, BALL_RADIUS, TFT_BLACK);
    tft.fillCircle(xNew, yNew, BALL_RADIUS, TFT_WHITE);
  }
}

void setDisplay(bool onoff) {
  display_status = onoff;
  if (!onoff) sleep_pos = sampledAngle.load();
  digitalWrite(BLK_PIN, onoff ? HIGH : LOW);
}
