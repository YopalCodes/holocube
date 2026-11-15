#pragma once
// ---- Hardware config: edit here to match your wiring ----
// XIAO ESP32-S3 pad -> GPIO:  D0=1 D1=2 D2=3 D3=4 D4=5(SDA) D5=6(SCL) D8=7(SCK) D9=8 D10=9(MOSI)

// ST7789VW LCD (SPI)
#define LCD_SCK    7    // D8
#define LCD_MOSI   9    // D10
#define LCD_DC     2    // D1
#define LCD_CS     -1   // module has no CS pin -> SPI mode 3 (set to 1 / D0 if you use a module with CS)
#define LCD_RST    3    // D2
#define LCD_BL     4    // D3  (-1 if backlight tied high)
#define LCD_W      240
#define LCD_H      240  // 280 for 1.69" panels (then LCD_OFFSET_Y 20)
#define LCD_OFFSET_Y 0
#define LCD_INVERT true // most ST7789VW IPS panels need inversion; flip if colours look negative
#define LCD_SPI_HZ 80000000

// MPU6050 (I2C)
#define IMU_SDA    5    // D4
#define IMU_SCL    6    // D5
#define IMU_ADDR   0x68

// Gesture axes: roll = atan2(accel[ROLL_AXIS], accel[UP_AXIS] * UP_SIGN) * ROLL_SIGN   (axis 0=x 1=y 2=z)
// Run tools/calibrate_roll.py and tools/calibrate_bob.py to measure these.
#define IMU_ROLL_AXIS 1
#define IMU_UP_AXIS   2
#define IMU_UP_SIGN   1   // -1 if the IMU is mounted upside down
#define IMU_ROLL_SIGN 1   // set -1 if left/right are swapped

// Gesture tuning
#define ROLL_TRIGGER_DEG 32.0f   // tilt needed to switch apps
#define ROLL_HOLD_MS     120     // must stay tilted this long
#define ROLL_REARM_DEG   12.0f   // must return this close to level before next switch
#define GESTURE_COOLDOWN 500
#define BOB_G            0.68f   // vertical accel spike (g) counted as one bob
#define BOB_MIN_GAP_MS   150
#define BOB_WINDOW_MS    670     // second bob must arrive within this window
#define BOB_MAX_TILT_DEG 18.0f   // ignore bobs while tilted

#define QR_SHOW_MS       20000
#define DEFAULT_URL      "https://yopalcodes.github.io/holocube/"
#define DEFAULT_LAT      35.7847f   // NC State
#define DEFAULT_LON      -78.6821f
#define TZ_EASTERN       "EST5EDT,M3.2.0,M11.1.0"
#define GIF_PATH         "/gif.gif"
#define GIF_MAX_BYTES    (2 * 1024 * 1024)
#define BLE_NAME         "HoloCube"
