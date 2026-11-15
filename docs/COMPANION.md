# SmartKnob companion — v0.1

A native macOS menu bar app and configurable ESP32 firmware. Requires macOS 14 or later and the Arduino dependencies in the main README. Images and the four page types remain compiled into firmware; ordering, enabled pages, detents, shortcuts and sleep are editable.

## Build and open

From the repository root:

```sh
./scripts/build-macos.sh
open build/SmartKnob.app
```

The app is locally ad-hoc signed, not notarised. For daily use, copy `build/SmartKnob.app` into Applications and launch it there before enabling **Launch app at login**. Rebuilds can cause macOS to request Bluetooth/Automation permissions again. No Accessibility permission is required: Discord/system media shortcuts originate on the knob.

The app opens a configuration window and stays in the menu bar after that window closes. **Open SmartKnob…** brings it back. **Quit SmartKnob** stops the host integration. `--preview` launches the interface without starting Bluetooth or polling controls.

## Firmware and first connection

1. Build/upload `src/ESP32/ESP32.ino` using the existing ESP32 Dev Module board settings. No extra Arduino libraries are needed; Preferences is provided by ESP32.
2. Pair **Haptic Knob** in macOS Bluetooth settings to retain the keyboard HID connection.
3. Open SmartKnob, allow Bluetooth access, then select the discovered knob and **Connect**. Subsequent launches reconnect to that device automatically.
4. The app reads the configuration stored on the knob. Change settings and select **Apply to knob**. A successful acknowledgement means validation and NVS persistence completed. The firmware changes pages/settings immediately, without rebooting.

The new custom GATT service changes the Bluetooth service table. If macOS keeps a cached old service list, forget **Haptic Knob**, power-cycle it and pair again. Do not erase bonds as a routine recovery step.

Uploaded to the connected ESP32 through Arduino IDE on **15.11.2026**. Upload completed with “Hash of data verified” and a hardware reset. Runtime diagnostics confirmed an encrypted Bluetooth connection, `ready=1 keyboard=1 consumer=1`, zero notification failures, and 5,874 touch reads with zero errors. Physical haptic feel, endpoint direction and companion-app controls still require the checks below.

## Controls

| Page | Operation | Configuration |
| --- | --- | --- |
| Volume | Absolute output-volume targets; real values read from the current default audio output | Step, arc, detent strength and sharpness |
| Brightness | Absolute display-brightness targets; real values read from the selected display | Step, arc, strength, sharpness; display selected on Mac |
| Media | Turn for next/previous, tap for play/pause | Notches/revolution, strength, sharpness; system HID, Music or Spotify |
| Discord | Three existing positions: unmute, mute, deafen; tap to disconnect | Strength, sharpness, three keyboard shortcuts |

Swipe to move through enabled pages. Use the sidebar arrows to reorder them. At least one page must stay enabled. The first enabled page opens after boot or applying a configuration. One instance of each of the four presets is supported.

Volume/brightness offer **Low (2%)** and **High (4%)** under Change per notch. Low gives 51 positions and High gives 26, including both endpoints. Legacy 1%/2% settings migrate to Low; larger legacy steps migrate to High, preserving all other configuration. Turns select the configured percentage grid, including exact 0% and 100%. An externally set value between grid positions is read accurately; turning selects the next grid position.

Travel is 90–300°, default 270°. Positions use an **unwrapped encoder angle**, so pushing past an endpoint never wraps the value around. Endpoint resistance uses the selected motor strength. These are haptic stops, not mechanical locks: enough hand force can overcome them, but the commanded percentage still clamps to 0–100%.

On page entry and after external value changes, the knob gently moves to the nearest detent for the current host value. Positioning and holding use the exact same detent centre. With High (4%), 50% has no exact detent: the knob uses the 52% notch (ties round upward), while the display and Mac retain the actual 50% until a real input. Low (2%) has an exact 50% detent.

Automatic positioning is limited to 0.35 V. A sustained deliberate movement farther away from the target hands control back to normal detents; small overshoots are ignored. If positioning still has not settled after 2.5 s, it also transitions to normal detents at the current angle instead of remaining limp. The handover itself emits no input; subsequent detent crossings work normally, and bounded endpoints remain active. At strength zero, automatic positioning and endpoint resistance are disabled; the current value's detent is anchored to the current physical angle instead.

The strength slider spans 0–0.80 V, displayed as 0–100%. Existing default strengths are retained: volume 0.50 V, media/brightness 0.40 V, Discord 0.60 V. Sharpness controls torque buildup independently. Motor voltage is not a calibrated physical torque measurement.

Sleep defaults to 10 min; 0 means never. Sleep turns off the display and motor torque. Touch or rotate to wake. Page/config changes and deliberate input restart the timer; passive host polling does not.

## Host support and fallbacks

- **Volume:** Core Audio controls the current default output when it exposes writable scalar volume. Devices with a fixed output, such as some HDMI or USB outputs, are reported unavailable. Separate stereo controls retain their channel balance.
- **Brightness:** dynamically resolved `DisplayServicesGetBrightness` / `DisplayServicesSetBrightness`, targeting the selected display. This is a private macOS API and can change; unsupported displays are reported unavailable. External DDC/CI monitor support is not implemented. Read success does not prove write support: a failed write disables that control until reconnect.
- **No host feedback:** volume/brightness show unavailable and release torque. They do not fall back to estimated percentages or blind HID keys. This is intentional: meaningful endpoints require authoritative values.
- **Media:** System media keys is the default and works without the app via HID. Select Apple Music or Spotify for app-controlled playback. Those players must already be running and may request Automation permission. Script errors are shown; the same command is not retried through HID, which could double-execute an uncertain action. Queued media commands are bounded and expire.
- **Discord:** always HID, using the existing release/reconnect worker. Defaults: F13 (toggle mute), Option+F13 (toggle deafen), Shift+Option+F13 (disconnect). Configure matching shortcuts in Discord itself. Its actual mute state is not readable through HID; manual changes in Discord can desynchronise the three-position indication, as in the original design. Selection survives page changes, not a device reboot.

General settings and connection diagnostics have their own sidebar page. Page settings live on the knob. Player/display choice and login behaviour belong to this Mac and apply immediately. The app needs no network service or cloud account.

## BLE protocol v1

Service: `7b4e1000-8e5a-4d2b-a6b9-4d534b4e4f42`.

| Characteristic suffix | Purpose |
| --- | --- |
| `1001` | Encrypted writes with response; command packets ≤20 bytes |
| `1002` | Device event notifications |
| `1003` | Encrypted configuration read, 38 bytes (Core Bluetooth handles long reads) |

All multi-byte integers are little-endian. Page indices: volume=0, media=1, brightness=2, Discord=3. Firmware's legacy display/icon IDs are separate.

Config bytes: version (1 byte), sleep minutes (u16), enabled count (1), full page permutation (4), four page records (6 each: step, strength in centivolts, sharpness in percent, circular notch count, arc degrees u16), three shortcuts (modifier byte + HID usage each). Discord's circular notch count is fixed at 3. Configuration validation is shared semantically between C++ and Swift and checked against a common byte/hash fixture.

| Direction | Packet |
| --- | --- |
| App → knob | `01 capabilities` heartbeat; bits 0/1/2 = volume/brightness/app media |
| App → knob | `02 page value:u16 ackSequence:u16`; value in 0–10000 |
| App → knob | `03 mediaAction` explicit HID media command: pause=0, next=1, previous=2 |
| App → knob | `04` start companion session; clear stale pending acknowledgements |
| App → knob | `0A transaction`, `0B transaction offset data…`, `0C transaction hash:u32` staged Apply |
| Knob → app | `80 sequence:u16 page action value:u16`; action set=0, pause=1, next=2, previous=3 |
| Knob → app | `81 version page hidReady awake configHash:u32` diagnostics |
| Knob → app | `82 transaction result`; result 0=success, 1=rejected/storage failure |

Config checksum is FNV-1a, used for transfer integrity, not authentication. Link encryption protects writes. Chunks must be contiguous and belong to the same transaction; full validation/hash checking and persistence occur before activation. Incomplete, invalid or expired transfers leave the current configuration active. Reapplying identical settings avoids flash writes.

The app sends a heartbeat/state snapshot every 500 ms and the knob sends diagnostics every second. A four-second app lease prevents simultaneous BLE/HID media ownership. Values expire after three seconds without host snapshots. Sequence acknowledgements prevent old snapshots from pulling back a recent turn; after a lost final event, the host value reconciles after a one-second quiet period. BLE callbacks only enqueue RAM packets; NVS writes occur in the main loop. The motor FOC task remains independent.

The motor task owns encoder sampling, torque computation, detent crossings and centring, running on core 1 at priority 3 with a 1 ms target period. Its damped spring replaces the steep cube-root centring law and tangent detents. Velocity is filtered; a small deadband suppresses encoder-noise chatter. Late control cycles (>20 ms) output zero torque, and a stalled main loop loses its torque lease after one second. Control snapshots and commands use short protected copies and a one-entry mailbox; the motor never waits for rendering, BLE input dispatch or logging.

The same task owns touch I2C polling at 100 Hz and sends samples to the UI task, eliminating cross-task sensor-bus locks. Encoder transactions run at 400 kHz; touch probing/reads keep the board’s original 100 kHz timing, with bounded transaction timeouts. Only the sensor task changes bus speed. `[Perf] motor_hz=… max_gap_us=…` diagnostics report the measured loop rate and worst interval every five seconds; this is not a hard real-time guarantee.

Display rendering caches the static page in a 240×240 8-bit background sprite. Normal motion restores the old 13×13 marker region and draws a new 13×13 marker sprite; only a changed value label or page needs a larger transfer. Marker scheduling targets up to 60 Hz, independent of the motor task. Actual physical display rate and haptic feel need device testing.

## Performance revision verified on 15.11.2026

The optimised firmware was flashed through Arduino IDE and its written hash
verified. After the touch-clock correction, serial diagnostics recorded
`motor_hz=1000`, maximum measured intervals of 1,432–2,000 µs across the observed
five-second windows, and 955 successful touch reads with zero errors. Encrypted
keyboard/consumer HID subscriptions were ready with zero notification failures.
The rebuilt app was restarted and its two notch choices and separate General
page checked. These measurements establish control-loop timing in the observed
session; physical settling/feel still needs the user's turning test.

## Verification

```sh
./scripts/test-firmware.sh
./scripts/test-macos.sh
./scripts/build-macos.sh
```

The firmware script covers existing BLE lifecycle/input-release/touch behaviour, companion-only connections, configuration migration/transfers, host acknowledgement races, bounded endpoints, motor updates independent of rendering, damped centring on a simple simulated plant, manual/timeout handover without spurious input, exact grid-aligned centring (including 50% at High), and stale-cycle cutoffs. The Swift test checks the shared wire fixture, validation and page ordering. The standalone Swift harness runs with Command Line Tools and does not require Xcode's XCTest framework.

Build the ESP32 using Arduino IDE or:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 src/ESP32
```

Before adopting this on hardware, check:

1. Pairing/HID subscriptions and custom-service discovery together; app quit/relaunch and device power cycle.
2. Apply, change page order/keys, reboot and verify readback matches. Interrupt an Apply; confirm old settings survive or readback shows the fully committed new settings.
3. Both bounded pages at 0%, 50%, 100%: no input during repositioning, correct torque direction, no value wrap after pushing beyond a stop, and no stale feedback during a continuous fast turn.
4. Change volume/brightness elsewhere on the Mac, switch away/back, and confirm the displayed and physical value follows. Confirm unsupported outputs disable feedback.
5. Sleep/wake, Low/High detents, all Discord transitions/tap, and media in HID/Music/Spotify modes. Confirm one action per deliberate input.

Builds and host tests do not establish motor feel, radio reliability, physical endpoint direction or touchscreen behaviour on the device.
