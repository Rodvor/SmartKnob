// Note: Colors for fonts using RGB565

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SimpleFOC.h>
#include <math.h>
#include "NimBLEKeyboard.h"

#include "menu_config.h"
#include "pin_config.h"
#include "icons.h"

#define CST816S_ADDR        0x15
#define CST816S_FINGER_REG  0x02
#define CST816S_X_HIGH_REG  0x03
#define CST816S_X_LOW_REG   0x04
#define CST816S_SWIPE_THRESHOLD 40

MagneticSensorI2C encoder = MagneticSensorI2C(AS5600_I2C);
TFT_eSPI tft = TFT_eSPI();
BLDCMotor motor = BLDCMotor(7);
BLDCDriver3PWM driver = BLDCDriver3PWM(IN1, IN2, IN3, EN);
NimBLEKeyboard bleKeyboard;

bool last_button_state = HIGH;

const int cx = 120;
const int cy = 120;
const int radius = 120;
const int lineLength = 7;

float currentPos = 0;
float max_torque = 0.3;
float torque_build_up = 0.8;
int current_choice = 0;

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

unsigned long last_touch_time = 0;
const unsigned long TOUCH_DEBOUNCE_MS = 300;
bool touch_active = false;
int touch_start_x = -1;
int touch_end_x = -1;

float sleep_pos = 0;
#define WAKE_ROTATION_THRESHOLD (PI / 60.0)

TaskHandle_t focTask;

void focLoop(void* param) {
  for (;;) {
    motor.loopFOC();
    vTaskDelay(1);
  }
}

uint8_t readTouchReg(uint8_t reg) {
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(CST816S_ADDR, (uint8_t)1);
  if (Wire.available()) return Wire.read();
  return 0;
}

uint8_t readTouchFingers() { return readTouchReg(CST816S_FINGER_REG); }

int readTouchX() {
  uint8_t hi = readTouchReg(CST816S_X_HIGH_REG) & 0x0F;
  uint8_t lo = readTouchReg(CST816S_X_LOW_REG);
  return (hi << 8) | lo;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(ENCODER_SDA, ENCODER_SCL);
  encoder.init();

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(10);
  digitalWrite(TP_RST, HIGH);
  delay(50);
  pinMode(TP_INT, INPUT);
  Wire1.begin(TP_SDA, TP_SCL);

  driver.voltage_power_supply = 12.0;
  driver.init();
  motor.linkDriver(&driver);
  motor.linkSensor(&encoder);
  motor.controller = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  motor.voltage_limit = 3.0;
  motor.init();
  motor.initFOC();

  xTaskCreatePinnedToCore(focLoop, "FOC", 4096, NULL, 1, &focTask, 1);

  pinMode(BLK_PIN, OUTPUT);
  digitalWrite(BLK_PIN, HIGH);

  tft.init();
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xad75);

  bleKeyboard.begin("Haptic Knob");

  tft.fillScreen(TFT_BLACK);
  tft.drawBitmap(ICON_X, ICON_Y, icon_bluetooth, ICON_W, ICON_H, BLUETOOTH_COLOR);
  tft.drawString("Connecting...", 122, 180);

  while (!bleKeyboard.isConnected()) {
    delay(500);
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
  unsigned long now = millis();
  uint8_t fingers = readTouchFingers();

  if (fingers >= 1) {
    int x = readTouchX();
    if (!touch_active) { touch_active = true; touch_start_x = x; }
    touch_end_x = x;
  } else {
    if (touch_active) {
      touch_active = false;
      if (now - last_touch_time < TOUCH_DEBOUNCE_MS) return;
      last_touch_time = now;

      if (!display_status) { setDisplay(true); last_activity = now; return; }

      int dx = touch_end_x - touch_start_x;
      if      (dx >  CST816S_SWIPE_THRESHOLD) swipeToMenu(+1);
      else if (dx < -CST816S_SWIPE_THRESHOLD) swipeToMenu(-1);
      else { doTapAction(); last_activity = now; }
    }
  }
}

void loop() {
  bool button_state = digitalRead(BUTTON_PIN);
  if (button_state != last_button_state) {
    if (button_state == LOW) {
      if (display_status) { doTapAction(); last_activity = millis(); }
      else                { setDisplay(true); last_activity = millis(); }
    }
    last_button_state = button_state;
  }

  handleTouchRevised();

  if (new_ui_id != current_ui_id) {
    drawUI(new_ui_id);
    take_input = false;
  }

  float pos = encoder.getAngle();
  int choice = calc_choice(pos);

  if (take_input) {
    motor.move(calc_torque(pos));
  } else {
    float target_angle = 0;
    if (current_ui_id == DISCORD_ID) target_angle = discord_choice * 2 * PI / 3;
    motor.move(torque_to_angle(pos, target_angle));
    if (abs(((target_angle - PI / 2) - pos)) < 0.05) {
      take_input = true;
      current_choice = choice;
    }
  }

  if (pos != currentPos) {

    if (!display_status && fabs(pos - sleep_pos) > WAKE_ROTATION_THRESHOLD) {
      setDisplay(true);
      last_activity = millis();
    }

    drawBall(currentPos, TFT_BLACK);
    drawBall(pos, TFT_WHITE);

    int new_choice = calc_choice(pos);

    if (new_choice != current_choice && take_input) {
      if (pos > currentPos) {
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
          bleKeyboard.pressF13(); delay(20); bleKeyboard.pressAltF13();
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

void drawUI(int id) {
  tft.fillScreen(TFT_BLACK);
  current_ui_id = id;
  drawMenuIcon(id);
  tft.setFreeFont(&FreeSans12pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0xad75);
  if      (id == VOLUME_ID)  { tft.drawString("Volume",     120, 180); max_torque = VOLUME_TORQUE;  torque_build_up = VOLUME_BUILDUP;  n_lines = VOLUME_NOTCHES; }
  else if (id == MEDIA_ID)   { tft.drawString("Media",      120, 180); max_torque = MEDIA_TORQUE;   torque_build_up = MEDIA_BUILDUP;   n_lines = MEDIA_NOTCHES; }
  else if (id == DISCORD_ID) { tft.drawString("Discord",    120, 180); max_torque = DISCORD_TORQUE; torque_build_up = DISCORD_BUILDUP; n_lines = DISCORD_NOTCHES; }
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

void drawBall(float pos, uint16_t color) {
  int x = round(cx + (radius - 18) * cos(pos));
  int y = round(cy + (radius - 18) * sin(pos));
  tft.fillCircle(x, y, 5, color);
}

void setDisplay(bool onoff) {
  display_status = onoff;
  if (!onoff) sleep_pos = encoder.getAngle();
  digitalWrite(BLK_PIN, onoff ? HIGH : LOW);
}
