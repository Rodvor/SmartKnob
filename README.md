**Smart haptic knob as computer (MacOS) HID**

Still in development.


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

