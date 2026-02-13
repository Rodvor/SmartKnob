#include <TFT_eSPI.h>
#include <Wire.h>
#include <SimpleFOC.h>
#include <math.h>

// Get menu config and add extra config here.
#include "menu_config.h"

// Get ESP32 pin config
#include "pin_config.h"


// Encoder
MagneticSensorI2C encoder = MagneticSensorI2C(AS5600_I2C);

// Screen
TFT_eSPI tft = TFT_eSPI();

// SimpleFOC
BLDCMotor motor = BLDCMotor(7);   // set correct pole pairs
BLDCDriver3PWM driver = BLDCDriver3PWM(
  IN1,  // IN1
  IN2,  // IN2
  IN3,  // IN3
  EN   // EN
);

// Button state
bool last_state = HIGH;

// For drawing indents
const int cx = 120;        // center x
const int cy = 120;        // center y
const int radius = 120;    // distance from center
const int lineLength = 7;  // length of each line

// Variable setup for encoder and motor
float currentPos = 0;   // Current encoder position
float max_torque = 0.3; // Define some torque values, changed later...
float torque_build_up = 0.8;
int current_choice = 0;

// Variable setup for screen
bool display_status = true; // On/off
bool take_input = false;
int last_activity = 0;
int current_ui_id = -1;
int new_ui_id = VOLUME_ID;
int n_lines = 40;
int discord_choice = 0; // Normal 0, mute 1, deafen 2.
const int CHOICES[] = {VOLUME_ID, BRIGHT_ID, MEDIA_ID, DISCORD_ID}; // Menu choices listed in order from top -> clockwise

void setup() {

  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, RXD2, TXD2);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // use internal pull-up

  // Setup encoder
  Wire.begin(ENCODER_SDA, ENCODER_SCL);
  encoder.init();
  
  // Setup motor driver
  driver.voltage_power_supply = 12.0;  // motor supply
  driver.init();

  // Link driver and sensor
  motor.linkDriver(&driver);
  motor.linkSensor(&encoder);

  // Torque control 
  motor.controller = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  //motor.sensor_direction = Direction::CCW;

  motor.voltage_limit = 3.0;
  motor.init();
  motor.initFOC();

  // Display blacklight pins
  pinMode(BLK_PIN, OUTPUT);  // BLK-pin
  digitalWrite(BLK_PIN, HIGH);

  // Setup screen
  tft.init();
  tft.fillScreen(TFT_BLACK); // Black
  tft.setTextDatum(MC_DATUM); // Text alignment
  tft.setFreeFont(&FreeSansBold18pt7b); // Set font
  tft.setTextColor(TFT_WHITE); // Font to white

}

void loop() {

  // Mandatory for FOC
  motor.loopFOC();

  bool button_state = digitalRead(BUTTON_PIN);

  if (button_state != last_state) {

    if ( button_state == LOW ) {
      // Button has been pressed
      if (display_status) {
        // Do action if display is on
        buttonPressed();
      } else {
        // Only wake display if display is of
        setDisplay(true);
        last_activity = millis();
      }
    }

    last_state = button_state;
  }

  // If new ui has been selected (button press/idle) -> redraw
  if (new_ui_id != current_ui_id) {
    drawUI(new_ui_id);
    take_input = false;
  }

  // Read encoder
  float pos = encoder.getAngle();

  // Choice
  int choice = calc_choice(pos);

  // Calculate torque
  if (take_input) {
    // Check if menu has been initialized and ready for input
    float torque = calc_torque(pos);
    motor.move(torque);

  } else {
    // Move motor to menu default position
    float target_angle = 0;

    // Unless discord has is selected -> move to prev. choice
    if (current_ui_id == DISCORD_ID) {
      target_angle = discord_choice * 2*PI/3;
    }

    // Apply torque
    float torque = torque_to_angle(pos, target_angle); 
    motor.move(torque);

    // If we have reached close enough -> menu initialized
    if ( abs(((target_angle - PI/2) - pos)) < 0.05 ) {
      take_input = true;
      current_choice = choice;
    }

  }




  // Logic
  if (pos != currentPos) {
    // Dial has moved

    // Move ball indicator
    drawBall(currentPos, TFT_BLACK);
    drawBall(pos, TFT_WHITE);

    // Get the current choice
    int new_choice = calc_choice(pos);

    if (new_choice != current_choice && take_input) {
      // Current choice is different and we actually want input

      if (pos > currentPos) {
        // Increase
        if ( current_ui_id == VOLUME_ID ) {
          sendCmd("<volume-up>");
        } else if ( current_ui_id == MEDIA_ID ) {
          sendCmd("<skip>");
        } else if ( current_ui_id == BRIGHT_ID ) {
          sendCmd("<bright-up>");
        } 
      } else {
        // Decrease
        if ( current_ui_id == VOLUME_ID ) {
          sendCmd("<volume-down>");
        } else if ( current_ui_id == MEDIA_ID ) {
          sendCmd("<previous>");
        } else if ( current_ui_id == BRIGHT_ID ) {
          sendCmd("<bright-down>");
        } 
      }

      if ( current_ui_id == DISCORD_ID) {

        if (new_choice == 0) {
          // To top -> unmute by toggling mute
          sendCmd("<mute>");
        } else if ((new_choice == 1) && (current_choice == 0)) {
          // To mute from unmute -> mute
          sendCmd("<mute>");
        } else if ((new_choice == 2) && (current_choice == 0)) {
          // To deafen from unmute -> mute and deafen
          sendCmd("<mute-and-deafen>");
        } else {
          // Moved from mute -> deafen or deafen -> mute.
          sendCmd("<deafen>");
        }

        discord_choice = new_choice;

      }

      if ( !display_status ) {
        setDisplay(true);
      }

      last_activity = millis();
      current_choice = new_choice;

    }

    currentPos = pos;

  }

  if ( display_status && millis() - last_activity > IDLE_TIMEOUT * 60000) {
    new_ui_id = 0;
    setDisplay(false);
  }

  delay(1); 
}











void drawUI(int id) {

  // Reset
  tft.fillScreen(TFT_BLACK);
  current_ui_id = id;

  // id 0 = Volume adjustment
  if (id == VOLUME_ID) {

    // Volume
    tft.setCursor(60, 130);
    tft.println("Volume");

    max_torque = VOLUME_TORQUE;
    torque_build_up = VOLUME_BUILDUP;
    n_lines = VOLUME_NOTCHES;

    // Draw dial
    drawClockLines();
    return;

  }

  if (id == MENU_ID) {

    // Menu
    tft.setCursor(70, 130);
    tft.println("Menu");

    max_torque = MENU_TORQUE;
    torque_build_up = MENU_BUILDUP;
    n_lines = MENU_NOTCHES;

    // Draw dial
    drawClockLines();
    return;

  }

  // Media skip and shii
  if (id == MEDIA_ID) {


    tft.setCursor(65, 130);
    tft.println("Media");

    max_torque = MEDIA_TORQUE;
    torque_build_up = MEDIA_BUILDUP;
    n_lines = MEDIA_NOTCHES;

    // Draw dial
    drawClockLines();
    return;

  }

  if (id == DISCORD_ID) {

    // Discord

    tft.setCursor(60, 130);
    tft.println("Discord");

    max_torque = DISCORD_TORQUE;
    torque_build_up = DISCORD_BUILDUP;
    n_lines = DISCORD_NOTCHES;

    // Draw dial
    drawClockLines();
    return;

  }

  if (id == BRIGHT_ID) {


    tft.setCursor(28, 130);
    tft.println("Brightness");

    max_torque = 0.4;
    torque_build_up = 0.4;
    n_lines = 20;

    // Draw dial
    drawClockLines();
    return;

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
  const float inside_tan = 0.5 * n_lines * (pos + PI/2);

  float torque = amplitude * tan(inside_tan);

  if (torque > max_torque) return max_torque;
  if (torque < -max_torque) return -max_torque;

  return torque;
}

float torque_to_angle(float pos, float angle) {

  // Calculate torque to move to set angle. f(x) = a * x ^ (1/3)
  const float a = 0.5;
  const float x = (pos + PI/2 - angle);

  float torque = a * cbrt(x);

  return torque;
}

int calc_choice(float pos) {
  // Normalize angle to 0..2*PI
  float angle = pos + PI/2;
  while (angle < 0) angle += 2*PI;
  while (angle >= 2*PI) angle -= 2*PI;

  // Map to 0..n_lines-1
  int choice = ((int)round(angle / (2.0 * PI) * n_lines)) % n_lines;
  return choice;
}

void drawBall(float pos, uint16_t color) {

  int r_ball = radius - 18;
  int ball_size = 5;

  int x = round(cx + r_ball * cos(pos));
  int y = round(cy + r_ball * sin(pos));

  tft.fillCircle(x, y, ball_size, color);
}

void buttonPressed() {

  if ( current_ui_id != MENU_ID ) {
    new_ui_id = MENU_ID;
    return;
  }

  new_ui_id = CHOICES[current_choice];

}


void setDisplay(bool onoff) {

  display_status = onoff;

  if ( onoff) {
    digitalWrite(BLK_PIN, HIGH);
    return;
  }

  digitalWrite(BLK_PIN, LOW);
}


void sendCmd(const char* cmd) {
  Serial2.println(cmd);
}
