/*
  Rotary encoder as HID media controller (macOS compatible)
  ---------------------------------------------------------
  Rotation:  volume up/down  (with Shift + Option)
  Button:
    - Single click → F13
    - Hold (>0.5 s) → pause/play immediately

  

  Switch: F13
*/

#include <HID-Project.h>
#include <SoftwareSerial.h>

SoftwareSerial espSerial(8, 9); // RX, TX

#define CLK 2
#define DT 3
#define SW 4
#define SWITCH_PIN 6


int lastCLK;
int lastSW;

// --- click timing ---
bool buttonPressed = false;
bool isHold = false;

bool switchState = false;
bool lastSwitchState = false;
unsigned long lastSwitchChange = 0;
const unsigned long debounceTime = 30;
static unsigned long lastKeepalive = 0;





void setup() {

  Consumer.begin();
  BootKeyboard.begin();

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  lastCLK = digitalRead(CLK);
  lastSW  = digitalRead(SW);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  switchState = !digitalRead(SWITCH_PIN);
  lastSwitchState = switchState;

  Serial.begin(115200); // USB debug
  espSerial.begin(57600);

}






void loop() {

  // ---- Rotary push button ----
  int currentSW = digitalRead(SW);
  unsigned long now = millis();

  // Button pressed
  if (currentSW == LOW && !buttonPressed) {
    buttonPressed = true;
  }

  // Button released
  if (currentSW == HIGH && buttonPressed) {
    buttonPressed = false;

    if (!isHold) {
      clickEncoder(); // only click
    }

    isHold = false;

    delay(50);

  }

  lastSW = currentSW;


  // Encoder rotation
  int currentCLK = digitalRead(CLK);
  if (currentCLK != lastCLK) {
    if (digitalRead(DT) != currentCLK) {

      if (!buttonPressed) {
        // Only rotation
        volumeUp();
      } else {
        // Press and rotate
        skipNext();
        isHold = true;
      }
    } else {
      if (!buttonPressed) {
        // Only rotation
        volumeDown();
      } else {
        // Press and rotate
        skipPrevious();
        isHold = true;
      }
    }
  }
  lastCLK = currentCLK;





  // ---- On/Off Toggle Switch ----
  bool raw = !digitalRead(SWITCH_PIN);

  if (raw != lastSwitchState && (now - lastSwitchChange) > debounceTime) {
      lastSwitchChange = now;
      lastSwitchState = raw;

      if (raw != switchState) {
          switchState = raw;

          if (switchState) {
              switchOn();
          } else {
              switchOff();
          }
      }
  }

  // Function for keeping HID connected

  if (millis() - lastKeepalive > 30000) {
    BootKeyboard.releaseAll();
    lastKeepalive = millis();
  }

  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');

    handleCommand(cmd);
  }
}

// ------------------------------------------------------
// Helper functions
// ------------------------------------------------------

void handleCommand(String c) {
  Serial.print("Handling: ");
  Serial.println(c);

  if (c.indexOf("volume-up") != -1) {
    volumeUp();
  } 
  else if (c.indexOf("volume-down") != -1) {
    volumeDown();
  }
  else if (c.indexOf("skip") != -1) {
    skipNext();
  }
  else if (c.indexOf("previous") != -1) {
    skipPrevious();
  }
  else if (c.indexOf("mute-and-deafen") != -1) {
    clickF13();
    delay(20);
    clickAltF13();
  }
  else if (c.indexOf("mute") != -1) {
    clickF13();
  }
  else if (c.indexOf("deafen") != -1) {
    clickAltF13();
  }
  else if (c.indexOf("bright-up") != -1) {
    clickF15();
  }
  else if (c.indexOf("bright-down") != -1) {
    clickF14();
  }
}

void volumeUp() {
  BootKeyboard.press(KEY_LEFT_SHIFT);
  BootKeyboard.press(KEY_LEFT_ALT);
  delay(5);
  Consumer.write(MEDIA_VOLUME_UP);
  delay(5);
  BootKeyboard.releaseAll();
}

void volumeDown() {
  BootKeyboard.press(KEY_LEFT_SHIFT);
  BootKeyboard.press(KEY_LEFT_ALT);
  delay(5);
  Consumer.write(MEDIA_VOLUME_DOWN);
  delay(5);
  BootKeyboard.releaseAll();
}

// Mute
void clickF13() {
  BootKeyboard.press(KEY_F13);
  delay(5);
  BootKeyboard.release(KEY_F13);
}

// Deafen
void clickAltF13() {
  BootKeyboard.press(KEY_LEFT_ALT);
  BootKeyboard.press(KEY_F13);
  delay(5);
  BootKeyboard.releaseAll();
}

// Brightness down
void clickF14() {
  BootKeyboard.press(KEY_F14);
  delay(5);
  BootKeyboard.release(KEY_F14);
}

// Brightness up
void clickF15() {
  BootKeyboard.press(KEY_F15);
  delay(5);
  BootKeyboard.release(KEY_F15);
}

void pauseMedia() {
  Consumer.write(MEDIA_PLAY_PAUSE);
}

void clickLeft() {
  BootKeyboard.press(KEY_LEFT_ARROW);
  delay(5);
  BootKeyboard.release(KEY_LEFT_ARROW);
}

void clickRight() {
  BootKeyboard.press(KEY_RIGHT_ARROW);
  delay(5);
  BootKeyboard.release(KEY_RIGHT_ARROW);
}


// Button and switch binding

// Switch on state
void switchOn() {
  clickF13();
}

// Switch off state
void switchOff() {
  clickF13();
}

// Click encoder once
void clickEncoder() {
  pauseMedia();
}

// Hold encoder
void holdEncoder() {
  clickF14();
}

void skipNext() {
  Consumer.write(MEDIA_NEXT);
}

void skipPrevious() {
  Consumer.write(MEDIA_PREVIOUS);
}




