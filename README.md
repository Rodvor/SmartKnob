**Smart haptic knob as computer (MacOS) HID**

Still in development.

## Build dependencies

The following older versions are the **known-good fallback**:

| Component | Version | Notes |
| --- | --- | --- |
| [Arduino IDE](https://www.arduino.cc/en/software) | 2.3.6 | Includes Arduino CLI 1.2.0 |
| [Espressif Arduino-ESP32](https://github.com/espressif/arduino-esp32) | 3.2.1 | Board: `ESP32 Dev Module` (`esp32:esp32:esp32`) |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | 2.3.9 | Install through Arduino Library Manager |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | 2.5.43 | Install through Arduino Library Manager; configuration below is required |
| [Simple FOC](https://docs.simplefoc.com) | 2.3.5 | Install through Arduino Library Manager |

`Wire`, `LittleFS`, and FreeRTOS are supplied by the Arduino-ESP32 board package and do not need separate installation.

To restore the known-good versions with Arduino CLI:

```sh
arduino-cli core install esp32:esp32@3.2.1
arduino-cli lib install "NimBLE-Arduino@2.3.9" "TFT_eSPI@2.5.43" "Simple FOC@2.3.5"
```

After restoring TFT_eSPI, replace its `User_Setup.h` with the repository copy at [`config/TFT_eSPI_User_Setup.h`](config/TFT_eSPI_User_Setup.h).

### Current upgraded versions

Installed, compile-checked, and flash-verified on 2026-09-03:

| Component | Current version |
| --- | --- |
| Espressif Arduino-ESP32 | 3.3.11 |
| NimBLE-Arduino | 2.5.1 |
| TFT_eSPI | 2.5.43 (already current) |
| Simple FOC | 2.4.0 |

The complete upgraded set compiles successfully for `ESP32 Dev Module`. ESP32 core 3.3.11 also compiles with the known-good older libraries, but upgrading ESP32 and Simple FOC together is recommended because Simple FOC 2.4.0 explicitly adds compatibility with all Arduino-ESP32 3.x releases.

NimBLE-Arduino 2.4 and newer starts registered HID services through `NimBLEServer::start()`. Its old `NimBLEHIDDevice::startServices()` method is deprecated and scheduled for removal. `NimbleKeyboard.h` detects the NimBLE generation: it uses the server method with the current library, but retains the HID-specific call when compiling against the fallback NimBLE 2.3.9.

Compilation and flash verification prove API compatibility and image integrity, but not the hardware behavior. Before making the newer versions the new baseline, test Bluetooth pairing/reconnection, fine volume adjustment, display output, motor direction, and haptic torque on the physical device. Simple FOC 2.4.0 changes ESP32 PWM safety/timing and velocity calculation, while ESP32 core 3.3.11 moves from ESP-IDF 5.4 to 5.5.5.

### Duplicate library files after an update

If a build error names files such as `HybridStepperMotor 2.cpp` or `NimBLEDevice 2.cpp`, the Arduino library directory contains duplicate conflict copies. Arduino compiles every `.cpp` file below a library's `src` directory, so these old copies are compiled alongside the newly installed version and cause duplicate definitions or incompatible APIs.

Move the `* 2.cpp` and `* 2.h` files out of the active library directory, or remove and cleanly reinstall that library. Do not fix this by editing the duplicated source files: they are stale library copies rather than project code.

### TFT_eSPI configuration

TFT_eSPI uses a library-level configuration that can be overwritten by an update or reinstall. The canonical setup is stored in [`config/TFT_eSPI_User_Setup.h`](config/TFT_eSPI_User_Setup.h); copy it over `Arduino/libraries/TFT_eSPI/User_Setup.h`.

```cpp
#define GC9A01_DRIVER

#define TFT_CS    5
#define TFT_DC    4
#define TFT_RST   0
#define TFT_MOSI  16
#define TFT_SCLK  15
#define TFT_BL    2

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
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
