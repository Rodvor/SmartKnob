**Smart haptic knob as computer (MacOS) HID**

Still in development.

## Build dependencies

Known-good versions currently used to build the firmware:

| Component | Version | Notes |
| --- | --- | --- |
| [Arduino IDE](https://www.arduino.cc/en/software) | 2.3.6 | Includes Arduino CLI 1.2.0 |
| [Espressif Arduino-ESP32](https://github.com/espressif/arduino-esp32) | 3.2.1 | Board: `ESP32 Dev Module` (`esp32:esp32:esp32`) |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | 2.3.9 | Install through Arduino Library Manager |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | 2.5.43 | Install through Arduino Library Manager; configuration below is required |
| [Simple FOC](https://docs.simplefoc.com) | 2.3.5 | Install through Arduino Library Manager |

`Wire`, `LittleFS`, and FreeRTOS are supplied by the Arduino-ESP32 board package and do not need separate installation.

### TFT_eSPI configuration

TFT_eSPI uses a library-level configuration that can be overwritten by an update or reinstall. Set these values in `Arduino/libraries/TFT_eSPI/User_Setup.h`:

```cpp
#define GC9A01_DRIVER

#define TFT_CS    5
#define TFT_DC    4
#define TFT_RST   0
#define TFT_MOSI  16
#define TFT_SCLK  15

#define LOAD_GFXFF
#define SPI_FREQUENCY       27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000
```


ESP32 to drive TFT touch display
SimpleFOC mini v1.0 to drive brushless motor
AS5600 magnetic encoder to read brushless motor angle

ESP32 works as a bluetooth keyboard and takes input from dial and touch input from screen. 
Screen next to knob models are final for now.

**Current controls:**
- Move between menus using swipe gesture left or right
- Volume menu: Dial for adjustment, tap for play/pause
- Discord menu: Dial for unmute, mute, deafen and tap for leaving VC
- Brightness menu: Dial for adjusting screen brightness
- Media menu: Dial for skip/previous media track, and tap for play/pause.

Volume and media are controlled using dedicated buttons e.g. KEY_MEDIA_VOLUME_UP

**Brightness (Mac):** 
- Up: F15
- Down: F14

**Keybinds for Discord (Configured in Discord Keybinds):**
- F13 -> Mute
- Alt + F13 -> Deafen
- Shift + Alt + F13 -> Disconnect





Images are from google and no rights have been requested. Personal use only.
