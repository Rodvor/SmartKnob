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

`Wire` and FreeRTOS are supplied by the Arduino-ESP32 board package and do not need separate installation.

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

### Bluetooth recovery and diagnostics

The 2026-09-27 connection fix handles authentication and restored HID subscriptions
arriving before, or without, `onConnect()`. A later connection callback preserves
those subscriptions. Input is ready only when the link is encrypted and both
keyboard and consumer notifications are enabled.

Bluetooth callbacks queue diagnostics in RAM; `poll()` prints them over USB at
115200 baud. Runtime LittleFS logging and the startup log dump were removed so
connection callbacks no longer perform flash writes. Capture the serial output
when diagnosing a problem; new logs do not survive a power cycle. Opening this
board's USB serial port can reset it.

`poll()` runs while waiting for the first connection and during normal operation.
It retries stopped advertising, clears connection state if the stack has lost the
link, and disconnects an incomplete HID link after 15 seconds so macOS can retry
with the existing bond. Failed key-release reports also trigger recovery before
more presses are sent. Pairings are not erased.

Once a connection has been ready for setup and at least five seconds have elapsed
since connection detection, it requests a 15–30 ms interval, zero peripheral
latency, and a 6-second supervision timeout. This is a single delayed request per
connection; macOS may reject it. These values satisfy
[Apple's connection parameter guidance](https://developer.apple.com/library/archive/qa/qa1931/_index.html).

A healthy diagnostic line has `connected=1 encrypted=1 ready=1 keyboard=1 consumer=1`.
`sent` counts notifications accepted by the local stack, not proof that macOS
acted on them. Check actual volume/media/brightness behavior as well.

Host regression checks (requires a C++17 compiler):

```sh
c++ -std=c++17 -Itests/ble_fakes tests/ble_lifecycle.cpp -o /tmp/smartknob-ble-test
/tmp/smartknob-ble-test
```

The checks exercise reordered/missing callbacks, subscription restoration,
stale reports, incomplete-link and advertising recovery, failed key releases,
and delayed parameter negotiation. They do not simulate radio behavior.

### Input stability and complete key releases (2026-11-03)

Built and flashed to the connected ESP32-D0WD-V3 on 2026-11-03. The uploader
verified the written image, and serial output confirmed the new build booted
and restored encrypted keyboard and consumer subscriptions. Both host regression
suites pass. This does not replace testing physical turns and taps.

Rotation uses a 15% notch-width hysteresis margin beyond the midpoint, with
150 ms of settling after menu centering. Encoder reads and motor updates share
one task. Touch startup probes the CST816 at address `0x15` on `Wire` first
(the bus used by the earlier working firmware), then `Wire1`. The selected bus
and chip ID are logged. On the connected knob, runtime diagnostics confirmed
`Wire` on GPIO 21/22 with chip ID `0xB6`; GPIO 19/18 in `pin_config.h` do not
match its actual touch wiring. Shared encoder/touch transactions are serialized.
Polling disables CST816 automatic sleep through register `0xFE`.

Touch recovery correction: the initial update assumed the controller used
`Wire1` solely from the pin definitions and introduced restrictive duration
checks. Touch now accepts short taps and slow swipes. Failed reads while idle
do not block the next touch; failed reads during a gesture discard that gesture
until a real release. A 300 ms debounce still prevents repeated activations.
Serial `[Touch]` diagnostics report valid reads, errors, and recognized gestures.
The correction was flashed and verified; runtime sampling reported zero touch
read errors, and the user confirmed both swipes and taps work.

Each queued input owns its complete press/release sequence. Fine volume still
uses Shift+Option, but releases both modifiers after every tick. Report phases
are spaced by 30 ms. The queue holds at most eight actions, expires unstarted
presses older than 250 ms, and clears waiting actions when changing menus.
Releases do not expire or depend on free queue slots. Failed releases and
asynchronous notification errors block further presses and trigger reconnect
recovery. During very fast turns or a congested link, excess ticks may be dropped
to avoid delayed adjustments.

Additional host regression checks:

```sh
c++ -std=c++17 -Itests/ble_fakes tests/input_actions.cpp -o /tmp/smartknob-input-test
/tmp/smartknob-input-test
c++ -std=c++17 tests/touch_input.cpp -o /tmp/smartknob-touch-test
/tmp/smartknob-touch-test
```

These cover rotation hysteresis and direction across wraparound, complete
keyboard/volume release sequences, stale inputs, queue overload and cancellation,
and release/error recovery. Physical movement and host-side HID behavior still
need checking on the device.

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
