#include <HID-Project.h>
#include <SoftwareSerial.h>

SoftwareSerial espSerial(8, 9); // RX, TX

void setup() {
  Serial.begin(115200); // USB debug
  espSerial.begin(115200);
}

void loop() {
  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');
    Serial.println("Received: " + cmd);

    handleCommand(cmd);
  }
}

void handleCommand(String c) {
  Serial.print("Handling: ");
  Serial.println(c);

  if (c.indexOf("up") != -1) {
    volumeUp();
  } 
  else if (c.indexOf("down") != -1) {
    volumeDown();
  } 
  else if (c.indexOf("mute") != -1) {
    clickF13();
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

void clickF13() {
  BootKeyboard.press(KEY_F13);
  delay(5);
  BootKeyboard.release(KEY_F13);
}

void clickF14() {
  BootKeyboard.press(KEY_F14);
  delay(5);
  BootKeyboard.release(KEY_F14);
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
