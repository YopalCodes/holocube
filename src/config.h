#pragma once
// ---- Hardware config: edit here to match your wiring ----
// XIAO ESP32-S3 pad -> GPIO:  D0=1 D1=2 D2=3 D3=4 D4=5 D5=6 D6=43 D7=44 D8=7 D9=8 D10=9

// ST7789VW LCD (SPI)
#define LCD_SCK    7    // D8
#define LCD_MOSI   5    // D4  (the LCD's "SDA" pin)
#define LCD_DC     2    // D1
#define LCD_CS     -1   // module has no CS pin -> SPI mode 3 (set to 1 / D0 if you use a module with CS)
#define LCD_RST    3    // D2
#define LCD_BL     4    // D3  (-1 if backlight tied high)
#define LCD_W      240
#define LCD_H      240  // 280 for 1.69" panels (then LCD_OFFSET_Y 20)
#define LCD_OFFSET_Y 0
#define LCD_INVERT true // most ST7789VW IPS panels need inversion; flip if colours look negative
#define LCD_SPI_HZ 80000000
#define DEFAULT_ROT 5   // 0-3 = 0/90/180/270 deg clockwise, 4-7 = mirrored; the control page setting overrides this

// MPU6050 (I2C)
#define IMU_SDA    8    // D9
#define IMU_SCL    9    // D10
#define IMU_ADDR   0x68

// Gesture axes: roll = atan2(accel[ROLL_AXIS], accel[UP_AXIS] * UP_SIGN) * ROLL_SIGN   (axis 0=x 1=y 2=z)
// Run tools/calibrate_roll.py to measure these.
#define IMU_ROLL_AXIS 0
#define IMU_UP_AXIS   2
#define IMU_UP_SIGN   1   // -1 if the IMU is mounted upside down
#define IMU_ROLL_SIGN 1   // set -1 if left/right are swapped

// Gesture tuning
#define ROLL_TRIGGER_DEG 32.0f   // tilt needed to switch apps
#define ROLL_HOLD_MS     120     // must stay tilted this long
#define ROLL_REARM_DEG   12.0f   // must return this close to level before next switch
#define GESTURE_COOLDOWN 500
// QR gesture: tilt toward you, back to level, toward you again (the same direction twice).
#define IMU_PITCH_AXIS   1       // the forward/back tilt axis (the one roll doesn't use)
#define PITCH_TRIGGER_DEG 20.0f  // how far each "toward" tilt must go
#define PITCH_LEVEL_DEG   8.0f   // "back" = return within this of level
#define PITCH_STEP_MS     500    // max time from leaving the first tilt to reaching the second
#define PITCH_MAX_ROLL_DEG 18.0f // ignored while tilted sideways

#define QR_SHOW_MS       20000
#define CLOCK_FX         1       // seasonal particles behind the clock (0 = off)
#define CLOCK_TILT_SIGN  -1      // -1 if clock particles slide the wrong way when you tilt
#define DEFAULT_URL      "https://yopalcodes.github.io/holocube/"
#define DEFAULT_LAT      35.7847f   // NC State
#define DEFAULT_LON      -78.6821f
#define TZ_EASTERN       "EST5EDT,M3.2.0,M11.1.0"
#define GIF_PATH         "/gif.gif"
#define GIF_MAX_BYTES    (2 * 1024 * 1024)
#define FS_RESERVE_BYTES (64 * 1024)   // always keep this much flash free; uploads that would eat into it are refused
#define BLE_NAME         "HoloCube"
