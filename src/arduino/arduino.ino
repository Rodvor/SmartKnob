#include <Arduino.h>
#include <Wire.h>
#include <SimpleFOC.h>
#include <SimpleFOCDrivers.h>

// ===== USER CONFIG =====
#define POLE_PAIRS 7
#define VOLTAGE_LIMIT 6.0   // voltage applied to motor
#define TORQUE_GAIN 2.0     // adjust for haptic strength
#define PIN_EN 16           // D0

// Motor / driver pins
#define PIN_IN1 14   // D5
#define PIN_IN2 12   // D6
#define PIN_IN3 13   // D7
#define PIN_SDA 4    // D2
#define PIN_SCL 5    // D1

// ===== SIMPLEFOC MINI INSTANCES =====
BLDCDriver3PWM driver(PIN_IN1, PIN_IN2, PIN_IN3, PIN_EN);
MagneticSensorI2C sensor = MagneticSensorI2C(AS5600_I2C);
BLDCMotor motor = BLDCMotor(POLE_PAIRS);

void setup() {
  Serial.begin(115200);

  // ===== I2C =====
  Wire.begin(PIN_SDA, PIN_SCL);
  sensor.init();

  // ===== EN pin =====
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH); // enable driver

  // ===== DRIVER =====
  driver.voltage_power_supply = 12;
  driver.pwm_frequency = 20000;
  driver.init();

  // ===== MOTOR =====
  motor.linkDriver(&driver);
  motor.linkSensor(&sensor);
  motor.controller = MotionControlType::torque; // torque mode
  motor.voltage_limit = VOLTAGE_LIMIT;

  // ===== FOC =====
  motor.init();
  motor.initFOC();

  Serial.println("SmartKnob ready!");
}

void loop() {
  motor.loopFOC(); // must run fast

  // ===== READ ANGLE =====
  float x = sensor.getAngle(); // 0 -> 2*PI

  // ===== COMPUTE TORQUE =====
  // Example: spring towards PI
  float torque = TORQUE_GAIN * (PI - x);

  // optional: clamp torque
  if (torque > VOLTAGE_LIMIT) torque = VOLTAGE_LIMIT;
  if (torque < -VOLTAGE_LIMIT) torque = -VOLTAGE_LIMIT;

  // ===== APPLY TORQUE =====
  motor.move(torque);

  // optional: debug
  // Serial.println(x); // comment out if loop too slow
}
