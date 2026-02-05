#ifndef MENU_CONFIG_H
#define MENU_CONFIG_H

// x_TORQUE: Maximum torque that can be applied (Volts)
// x_BUILDUP: How quickly torque builds up to maximum
// x_NOTCHES: How many indents/choices in menu

//
// Menu definitions
//



// Volume menu
#define VOLUME_NOTCHES 40
#define VOLUME_TORQUE 0.3
#define VOLUME_BUILDUP 0.8
#define VOLUME_ID 0

// Main menu
#define MENU_NOTCHES 4
#define MENU_TORQUE 1.0
#define MENU_BUILDUP 0.8
#define MENU_ID 1

// Media controls
#define MEDIA_NOTCHES 20
#define MEDIA_TORQUE 0.8
#define MEDIA_BUILDUP 0.8
#define MEDIA_ID 2

// Discord voice chat controls
#define DISCORD_NOTCHES 3
#define DISCORD_TORQUE 0.5
#define DISCORD_BUILDUP 0.7
#define DISCORD_ID 3

// Brightness controls
#define BRIGHT_NOTCHES 20
#define BRIGHT_TORQUE 0.4
#define BRIGHT_BUILDUP 0.4
#define BRIGHT_ID 4

#define IDLE_TIMEOUT 5 // Minutes until idle


#endif