// Note: Colors for fonts using RGB565

// Include libraries
#include <TFT_eSPI.h>
#include <Wire.h>
#include <SimpleFOC.h>
#include <math.h>
#include "NimbleKeyboard.h"
#include "InputFilter.h"
#include "TouchFilter.h"
#include "Companion.h"
#include "Haptics.h"

// Include configurations and icons
#include "menu_config.h"
#include "pin_config.h"
#include "icons.h"

#define CST816S_ADDR        0x15
#define CST816S_FINGER_REG  0x02
#define CST816S_X_HIGH_REG  0x03
#define CST816S_X_LOW_REG   0x04
#define CST816S_SWIPE_THRESHOLD 40

#define SCREEN_SIZE       240  // adjust if your panel isn't 240x240

// Define objects
MagneticSensorI2C encoder = MagneticSensorI2C(AS5600_I2C);
TFT_eSPI tft = TFT_eSPI();
BLDCMotor motor = BLDCMotor(7);
BLDCDriver3PWM driver = BLDCDriver3PWM(IN1, IN2, IN3, EN);
NimBLEKeyboard bleKeyboard;
Companion companion;
void addCompanion(NimBLEServer* s) { companion.begin(s); }
int page=0, current_menu_index=0, current_ui_id=-1, new_ui_id=VOLUME_ID;
const int CHOICES[]={VOLUME_ID,MEDIA_ID,BRIGHT_ID,DISCORD_ID};
float hostValue=0, currentPos=0, sleep_pos=0;
bool hostAvailable=false, display_status=true, take_input=false;
int discord_choice=0,current_choice=0;
uint32_t last_activity=0,lastFrame=0;
bool fullRedraw=true;
TFT_eSprite frame(&tft), marker(&tft);
bool frameReady=false;
TouchFilter touchFilter;
TwoWire* touchBus=nullptr;
uint32_t touchReadErrors=0,touchReadSuccesses=0,touchGestures=0;
#define WAKE_ROTATION_THRESHOLD (PI / 60.0)
TaskHandle_t focTask;
QueueHandle_t hapticCommands, hapticEvents, touchSamples;
struct TurnEvent { uint32_t epoch,at; int choice,delta; };
struct TouchSample { bool valid; uint8_t fingers; int x; uint32_t at; };
portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
knob::HapticState motorState;
std::atomic<uint32_t> motorCycles{0},motorMaxGapUs{0},controlLease{0};
knob::HapticState uiState;
uint32_t hapticEpoch=0;
bool readTouchSample(uint8_t& fingers,int& x);

void focLoop(void*) {
  knob::Haptics controller;
  knob::HapticCommand command;
  uint32_t previous=micros(), lastTouch=0;
  TickType_t wake=xTaskGetTickCount();
  for(;;) {
    uint32_t now=micros(), gap=now-previous; previous=now;
    if(gap>motorMaxGapUs.load()) motorMaxGapUs.store(gap);
    motor.loopFOC();
    float angle=encoder.getAngle();
    if(xQueueReceive(hapticCommands,&command,0)==pdTRUE) controller.configure(command,angle,millis());
    auto result=controller.update(angle,gap*0.000001f,millis());
    // A stalled app/UI task must not leave an indefinitely energised motor.
    if(uint32_t(millis()-controlLease.load())>1000) result.torque=0;
    motor.move(result.torque);
    if(result.delta) {
      TurnEvent event{result.epoch,millis(),result.choice,result.delta};
      // The event queue is bounded; motor control never waits for UI/BLE.
      xQueueSend(hapticEvents,&event,0);
    }
    portENTER_CRITICAL(&stateMux); motorState=result; portEXIT_CRITICAL(&stateMux);
    motorCycles.fetch_add(1);
    // This task owns both sensors on Wire, avoiding cross-task I2C locks.
    if(uint32_t(millis()-lastTouch)>=10) {
      lastTouch=millis(); TouchSample sample{}; sample.at=lastTouch;
      // CST816 on this board needs its established 100 kHz bus timing.
      // The same task owns both devices, so changing speed cannot race FOC.
      if(touchBus==&Wire) Wire.setClock(100000);
      sample.valid=readTouchSample(sample.fingers,sample.x);
      if(touchBus==&Wire) Wire.setClock(400000);
      xQueueSend(touchSamples,&sample,0);
    }
    vTaskDelayUntil(&wake,1); // 1 ms on the configured ESP32 FreeRTOS build
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
  companion.load();
  if (!bleKeyboard.begin("Haptic Knob", addCompanion)) {
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
  // Full-frame sprite avoids erasing icons underneath the position marker.
  frame.setColorDepth(8);
  frameReady=frame.createSprite(SCREEN_SIZE, SCREEN_SIZE)!=nullptr;
  marker.setColorDepth(16);
  frameReady=(marker.createSprite(13,13)!=nullptr) && frameReady;

  // Bluetooth connecting screen
  tft.fillScreen(TFT_BLACK);
  tft.drawBitmap(ICON_X, ICON_Y, icon_bluetooth, ICON_W, ICON_H, BLUETOOTH_COLOR);
  tft.drawString("Connecting...", 122, 180);


  Wire.begin(ENCODER_SDA, ENCODER_SCL);
  encoder.init();
  Wire.setClock(100000);
  Wire.setTimeOut(3);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(20);
  digitalWrite(TP_RST, HIGH);
  delay(100);
  pinMode(TP_INT, INPUT);
  Wire1.begin(TP_SDA, TP_SCL);
  Wire1.setTimeOut(3);
  initTouch();
  Wire.setClock(400000);

  driver.voltage_power_supply = 12.0;
  driver.init();
  motor.linkDriver(&driver);
  motor.linkSensor(&encoder);
  motor.controller = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;
  motor.voltage_limit = 3.0;
  motor.init();
  motor.initFOC();

  hapticCommands=xQueueCreate(1,sizeof(knob::HapticCommand));
  hapticEvents=xQueueCreate(32,sizeof(TurnEvent));
  touchSamples=xQueueCreate(8,sizeof(TouchSample));
  if(!hapticCommands || !hapticEvents || !touchSamples) { motor.disable(); while(true) delay(1000); }
  motorState.angle=encoder.getAngle();
  controlLease.store(millis());
  if(xTaskCreatePinnedToCore(focLoop,"FOC",4096,nullptr,3,&focTask,1)!=pdPASS) {
    motor.disable(); while(true) delay(1000);
  }

  last_activity=millis();
  new_ui_id=CHOICES[companion.config.order[0]];
  Serial.println("SmartKnob companion firmware ready");
}

void shortcut(int i) {
  auto s=companion.config.shortcuts[i];
  bleKeyboard.sendKeyReport(s.modifiers,s.key);
}
void media(int action) {
  // Ownership is negotiated by heartbeat. Never send HID as well as BLE.
  if(companion.active() && (companion.capabilities&4)) {
    companion.send(page,action+1); return;
  }
  if(action==0) bleKeyboard.playPause();
  else if(action==1) bleKeyboard.nextTrack();
  else bleKeyboard.prevTrack();
}
void doTapAction() {
  if(!take_input && isBounded()) { configureHaptics(); return; }
  if(page==0 || page==1) media(0);
  else if(page==3) shortcut(2);
}
void swipeToMenu(int direction) {
  current_menu_index=(current_menu_index+direction+companion.config.count)%companion.config.count;
  new_ui_id=CHOICES[companion.config.order[current_menu_index]];
  last_activity=millis();
}

void handleTouchRevised() {
  TouchSample sample;
  if(xQueueReceive(touchSamples,&sample,0)!=pdTRUE) return;
  const uint32_t now=sample.at;
  const bool valid=sample.valid;
  const uint8_t fingers=sample.fingers;
  const int x=sample.x;
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

bool isBounded() { return page==0 || page==2; }
void configureHaptics() {
  knob::HapticCommand c;
  c.epoch=++hapticEpoch; c.settings=companion.config.pages[page];
  c.page=page; c.value=hostValue; c.discordChoice=discord_choice;
  c.enabled=display_status && (!isBounded() || hostAvailable);
  xQueueOverwrite(hapticCommands,&c);
  take_input=false;
}
void enterPage() {
  bleKeyboard.cancelPendingInputs();
  current_ui_id=new_ui_id;
  page=companion.config.order[current_menu_index];
  hostAvailable=isBounded() && companion.available(page);
  hostValue=hostAvailable?companion.value(page):0;
  current_choice=page==3?discord_choice:0;
  configureHaptics(); fullRedraw=true;
}
void loop() {
  controlLease.store(millis());
  bleKeyboard.poll();
  static bool wasConnected=false;
  bool connected=bleKeyboard.isConnected();
  if(wasConnected && !connected) companion.resetLink();
  wasConnected=connected;
  companion.poll(bleKeyboard);
  portENTER_CRITICAL(&stateMux); uiState=motorState; portEXIT_CRITICAL(&stateMux);
  currentPos=uiState.angle;
  take_input=uiState.epoch==hapticEpoch && uiState.ready;
  while(uxQueueMessagesWaiting(touchSamples)) handleTouchRevised();
  if(companion.changed) {
    companion.changed=false; current_menu_index=0;
    new_ui_id=CHOICES[companion.config.order[0]]; current_ui_id=-1;
    last_activity=millis();
  }
  if(new_ui_id!=current_ui_id) enterPage();
  // Drain input before comparing host feedback so a new turn wins over an old snapshot.
  TurnEvent event;
  while(xQueueReceive(hapticEvents,&event,0)==pdTRUE) {
    if(event.epoch!=hapticEpoch || uint32_t(millis()-event.at)>250) continue;
    if(isBounded()) {
      hostValue=event.choice*companion.config.pages[page].step/100.0f;
      companion.send(page,0,uint16_t(lroundf(hostValue*10000)));
    } else if(page==1) media(event.delta>0?1:2);
    else if(event.choice!=current_choice) {
      if(event.choice==0) shortcut(0);
      else if(event.choice==1 && current_choice==0) shortcut(0);
      else if(event.choice==2 && current_choice==0) { shortcut(0); shortcut(1); }
      else shortcut(1);
      discord_choice=event.choice;
    }
    current_choice=event.choice; last_activity=millis();
  }
  if(isBounded()) {
    bool available=companion.available(page);
    if(available!=hostAvailable) {
      hostAvailable=available;
      if(available) hostValue=companion.value(page);
      configureHaptics(); fullRedraw=true;
    } else if(available && !companion.awaitingValue(page) && fabsf(companion.value(page)-hostValue)>0.005f && millis()-last_activity>350) {
      hostValue=companion.value(page); configureHaptics();
    }
  }
  if(!display_status && fabsf(currentPos-sleep_pos)>WAKE_ROTATION_THRESHOLD) {
    setDisplay(true); last_activity=millis();
  }
  if(display_status && companion.config.sleepMinutes && millis()-last_activity>uint32_t(companion.config.sleepMinutes)*60000) setDisplay(false);
  if(display_status && uiState.epoch==hapticEpoch && millis()-lastFrame>=16) {
    drawUI(current_ui_id); lastFrame=millis();
  }
  static uint32_t lastStatus=0,lastPerf=0;
  if(millis()-lastStatus>1000) { lastStatus=millis(); companion.status(page,bleKeyboard.isReady(),display_status); }
  if(millis()-lastPerf>=5000) {
    uint32_t elapsed=millis()-lastPerf; lastPerf=millis();
    Serial.printf("[Perf] motor_hz=%lu max_gap_us=%lu\n",(unsigned long)(motorCycles.exchange(0)*1000/elapsed),(unsigned long)motorMaxGapUs.exchange(0));
  }
  vTaskDelay(1);
}

void drawUI(int) {
  if(!frameReady) return;
  static int oldX=-1,oldY=-1,oldPercent=-1;
  static bool oldAvailable=false;
  const uint8_t* icons[]={icon_volume,icon_media,icon_brightness,icon_discord};
  uint16_t colors[]={VOLUME_COLOR,MEDIA_COLOR,BRIGHT_COLOR,DISCORD_COLOR};
  const char* names[]={"Volume","Media","Brightness","Discord"};
  bool rebuild=fullRedraw;
  if(rebuild) {
    frame.fillSprite(TFT_BLACK);
    frame.drawBitmap(ICON_X,ICON_Y,icons[page],ICON_W,ICON_H,colors[page]);
    frame.setTextDatum(MC_DATUM); frame.setTextColor(0xad75,TFT_BLACK);
    frame.setFreeFont(&FreeSans12pt7b); frame.drawString(names[page],120,180);
    if(page==3) {
      frame.drawBitmap(108,30,icon_unmute,SMALL_ICON_W,SMALL_ICON_H,UNMUTE_COLOR);
      frame.drawBitmap(178,155,icon_mute,SMALL_ICON_W,SMALL_ICON_H,MUTE_COLOR);
      frame.drawBitmap(44,155,icon_deafen,SMALL_ICON_W,SMALL_ICON_H,DEAFEN_COLOR);
    }
    oldX=-1; oldPercent=-1;
  }
  int percent=lroundf(hostValue*100);
  bool labelChanged=isBounded() && (rebuild || oldPercent!=percent || oldAvailable!=hostAvailable);
  if(labelChanged) {
    frame.fillRect(20,196,200,28,TFT_BLACK);
    frame.setTextDatum(MC_DATUM); frame.setTextColor(0xad75,TFT_BLACK);
    frame.setFreeFont(nullptr); frame.setTextFont(2);
    if(hostAvailable) { char text[8]; snprintf(text,sizeof(text),"%d%%",percent); frame.drawString(text,120,208); }
    else frame.drawString(companion.active()?"Control unavailable":"Open SmartKnob on Mac",120,208);
    oldPercent=percent; oldAvailable=hostAvailable;
  }
  // Restore scale marks after clearing the label: wide arcs put their endpoints
  // inside the label's update rectangle. Keep them in the cached background too.
  if(rebuild || labelChanged) {
    int lines=isBounded()?uiState.intervals+1:companion.config.pages[page].notches;
    for(int i=0;i<lines;i++) {
      float a=isBounded()?uiState.start+i*uiState.spacing:i*2*PI/lines-PI/2;
      frame.drawLine(lroundf(120+119*cosf(a)),lroundf(120+119*sinf(a)),lroundf(120+112*cosf(a)),lroundf(120+112*sinf(a)),0x9492);
    }
    if(isBounded()) for(int i=0;i<2;i++) {
      float a=uiState.start+i*uiState.intervals*uiState.spacing;
      // Three-pixel-wide, rounded endpoint ticks, inset from the display edge.
      for(int radius=110;radius<=118;radius++)
        frame.fillCircle(lroundf(120+radius*cosf(a)),lroundf(120+radius*sinf(a)),1,colors[page]);
    }
  }
  if(rebuild) { frame.pushSprite(0,0); fullRedraw=false; }
  else if(labelChanged) frame.pushSprite(20,196,20,196,200,28);
  int x=lroundf(120+102*cosf(currentPos)),y=lroundf(120+102*sinf(currentPos));
  if(!rebuild && !labelChanged && x==oldX && y==oldY) return;
  // Restore old pixels from the cached background, including any underlying icon.
  if(oldX>=0 && !rebuild) frame.pushSprite(oldX-6,oldY-6,oldX-6,oldY-6,13,13);
  for(int row=0;row<13;row++) for(int col=0;col<13;col++)
    marker.drawPixel(col,row,frame.readPixel(x-6+col,y-6+row));
  marker.fillCircle(6,6,5,TFT_WHITE); marker.pushSprite(x-6,y-6);
  oldX=x; oldY=y;
}
void setDisplay(bool onoff) {
  display_status=onoff;
  if(!onoff) sleep_pos=currentPos;
  else fullRedraw=true;
  configureHaptics();
  digitalWrite(BLK_PIN,onoff?HIGH:LOW);
}
