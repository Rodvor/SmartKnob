#include <TFT_eSPI.h>
#include <Wire.h>
#include <SimpleFOC.h>
#include <math.h>

#include "menu_config.h"
#include "pin_config.h"

#define CST816S_ADDR        0x15
#define CST816S_FINGER_REG  0x02
#define CST816S_X_HIGH_REG  0x03  // bits [3:0] = X high
#define CST816S_X_LOW_REG   0x04
#define CST816S_SWIPE_THRESHOLD 40  // pixels to count as a swipe

MagneticSensorI2C encoder = MagneticSensorI2C(AS5600_I2C);
TFT_eSPI tft = TFT_eSPI();
BLDCMotor motor = BLDCMotor(7);
BLDCDriver3PWM driver = BLDCDriver3PWM(IN1, IN2, IN3, EN);

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

// Menus cycled by swipe — no MENU_ID
const int CHOICES[] = {VOLUME_ID, BRIGHT_ID, MEDIA_ID, DISCORD_ID};
const int N_CHOICES = 4;
int current_menu_index = 0; // index into CHOICES[]

// Touch state
unsigned long last_touch_time = 0;
const unsigned long TOUCH_DEBOUNCE_MS = 300;
bool touch_active = false;
int touch_start_x = -1;

// Read a register from CST816S
uint8_t readTouchReg(uint8_t reg) {
  Wire.beginTransmission(CST816S_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(CST816S_ADDR, (uint8_t)1);
  if (Wire.available()) return Wire.read();
  return 0;
}

// Read finger count
uint8_t readTouchFingers() {
  return readTouchReg(CST816S_FINGER_REG);
}

// Read X coordinate (10-bit)
int readTouchX() {
  uint8_t hi = readTouchReg(CST816S_X_HIGH_REG) & 0x0F;
  uint8_t lo = readTouchReg(CST816S_X_LOW_REG);
  return (hi << 8) | lo;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, RXD2, TXD2);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Encoder I2C
  Wire.begin(ENCODER_SDA, ENCODER_SCL);
  encoder.init();

  // Touch I2C
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

  pinMode(BLK_PIN, OUTPUT);
  digitalWrite(BLK_PIN, HIGH);

  tft.init();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextColor(TFT_WHITE);

  Serial.println("Scanning Wire...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found: 0x");
      Serial.println(addr, HEX);
    }
  }
  Serial.println("Done.");
  }

// Perform the tap action for the current menu
void doTapAction() {
  if (current_ui_id == VOLUME_ID) {
    sendCmd("<play-pause>");
  } else if (current_ui_id == MEDIA_ID) {
    sendCmd("<play-pause>");
  } else if (current_ui_id == DISCORD_ID) {
    sendCmd("<disconnect>");
  }
  // BRIGHT_ID: do nothing
}

// Cycle menus left or right
void swipeToMenu(int direction) { // +1 = right, -1 = left
  current_menu_index = (current_menu_index + direction + N_CHOICES) % N_CHOICES;
  new_ui_id = CHOICES[current_menu_index];
  last_activity = millis();
}

void handleTouch() {
  unsigned long now = millis();
  uint8_t fingers = readTouchFingers();

  if (fingers >= 1) {
    int x = readTouchX();

    if (!touch_active) {
      // Finger just went down
      touch_active = true;
      touch_start_x = x;
    }
    // Finger still down — update last seen X (held in touch_start_x only at start)
  } else {
    if (touch_active) {
      // Finger just lifted — evaluate gesture
      touch_active = false;

      if (now - last_touch_time < TOUCH_DEBOUNCE_MS) return;
      last_touch_time = now;

      // Read current X one more time for end position
      // (fingers==0 so we use the last known start; instead track end_x)
      // Since we can't read X on release, we check swipe by tracking in the
      // active branch below — see note. For now use start_x saved approach:
      // This is handled by tracking end_x separately (see touch_end_x below).
    }
  }
}

// Revised approach — track start and end X properly
int touch_end_x = -1;

void handleTouchRevised() {
  unsigned long now = millis();
  uint8_t fingers = readTouchFingers();

  if (fingers >= 1) {
    int x = readTouchX();
    if (!touch_active) {
      touch_active = true;
      touch_start_x = x;
    }
    touch_end_x = x; // continuously update end position
  } else {
    if (touch_active) {
      touch_active = false;

      if (now - last_touch_time < TOUCH_DEBOUNCE_MS) return;
      last_touch_time = now;

      if (!display_status) {
        setDisplay(true);
        last_activity = now;
        return;
      }

      int dx = touch_end_x - touch_start_x;

      if (dx > CST816S_SWIPE_THRESHOLD) {
        // Swiped right
        swipeToMenu(+1);
      } else if (dx < -CST816S_SWIPE_THRESHOLD) {
        // Swiped left
        swipeToMenu(-1);
      } else {
        // Tap
        doTapAction();
        last_activity = now;
      }
    }
  }
}

void loop() {
  motor.loopFOC();

  // Physical button — now does tap action directly
  bool button_state = digitalRead(BUTTON_PIN);
  if (button_state != last_button_state) {
    if (button_state == LOW) {
      if (display_status) {
        doTapAction();
        last_activity = millis();
      } else {
        setDisplay(true);
        last_activity = millis();
      }
    }
    last_button_state = button_state;
  }

  // Touch
  handleTouchRevised();

  if (new_ui_id != current_ui_id) {
    drawUI(new_ui_id);
    take_input = false;
  }

  float pos = encoder.getAngle();
  int choice = calc_choice(pos);

  if (take_input) {
    float torque = calc_torque(pos);
    motor.move(torque);
  } else {
    float target_angle = 0;
    if (current_ui_id == DISCORD_ID) {
      target_angle = discord_choice * 2 * PI / 3;
    }
    float torque = torque_to_angle(pos, target_angle);
    motor.move(torque);
    if (abs(((target_angle - PI / 2) - pos)) < 0.05) {
      take_input = true;
      current_choice = choice;
    }
  }

  if (pos != currentPos) {
    drawBall(currentPos, TFT_BLACK);
    drawBall(pos, TFT_WHITE);

    int new_choice = calc_choice(pos);

    if (new_choice != current_choice && take_input) {
      if (pos > currentPos) {
        if (current_ui_id == VOLUME_ID)      sendCmd("<volume-up>");
        else if (current_ui_id == MEDIA_ID)  sendCmd("<skip>");
        else if (current_ui_id == BRIGHT_ID) sendCmd("<bright-up>");
      } else {
        if (current_ui_id == VOLUME_ID)      sendCmd("<volume-down>");
        else if (current_ui_id == MEDIA_ID)  sendCmd("<previous>");
        else if (current_ui_id == BRIGHT_ID) sendCmd("<bright-down>");
      }

      if (current_ui_id == DISCORD_ID) {
        if (new_choice == 0) {
          sendCmd("<mute>");
        } else if ((new_choice == 1) && (current_choice == 0)) {
          sendCmd("<mute>");
        } else if ((new_choice == 2) && (current_choice == 0)) {
          sendCmd("<mute-and-deafen>");
        } else {
          sendCmd("<deafen>");
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

  delay(1);
}

// --- UI ---

void drawUI(int id) {
  tft.fillScreen(TFT_BLACK);
  current_ui_id = id;

  if (id == VOLUME_ID) {
    tft.setCursor(60, 130); tft.println("Volume");
    max_torque = VOLUME_TORQUE; torque_build_up = VOLUME_BUILDUP; n_lines = VOLUME_NOTCHES;
    drawClockLines(); return;
  }
  if (id == MEDIA_ID) {
    tft.setCursor(65, 130); tft.println("Media");
    max_torque = MEDIA_TORQUE; torque_build_up = MEDIA_BUILDUP; n_lines = MEDIA_NOTCHES;
    drawClockLines(); return;
  }
  if (id == DISCORD_ID) {
    tft.setCursor(60, 130); tft.println("Discord");
    max_torque = DISCORD_TORQUE; torque_build_up = DISCORD_BUILDUP; n_lines = DISCORD_NOTCHES;
    drawClockLines(); return;
  }
  if (id == BRIGHT_ID) {
    tft.setCursor(28, 130); tft.println("Brightness");
    max_torque = BRIGHT_TORQUE; torque_build_up = BRIGHT_BUILDUP; n_lines = BRIGHT_NOTCHES;
    drawClockLines(); return;
  }
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
  const float a = 0.5;
  const float x = (pos + PI / 2 - angle);
  return a * cbrt(x);
}

int calc_choice(float pos) {
  float angle = pos + PI / 2;
  while (angle < 0) angle += 2 * PI;
  while (angle >= 2 * PI) angle -= 2 * PI;
  return ((int)round(angle / (2.0 * PI) * n_lines)) % n_lines;
}

void drawBall(float pos, uint16_t color) {
  int r_ball = radius - 18;
  int ball_size = 5;
  int x = round(cx + r_ball * cos(pos));
  int y = round(cy + r_ball * sin(pos));
  tft.fillCircle(x, y, ball_size, color);
}

void setDisplay(bool onoff) {
  display_status = onoff;
  digitalWrite(BLK_PIN, onoff ? HIGH : LOW);
}

void sendCmd(const char* cmd) {
  Serial2.println(cmd);
}