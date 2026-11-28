# HoloCube: Beam-Splitter Cube Display

XIAO ESP32-S3 + ST7789VW LCD + MPU6050. Firmware (PlatformIO/Arduino) in `src/`, control page (GitHub Pages) in `docs/`.

## Wiring (edit `src/config.h` if yours differs)

| LCD (ST7789VW) | XIAO pad | GPIO |   | MPU6050 | XIAO pad | GPIO |
|---|---|---|---|---|---|---|
| SCL/SCK | D8  | 7 | | SDA | D9 | 8 |
| SDA/MOSI | D4 | 5 | | SCL | D10 | 9 |
| DC | D1 | 2 | | VCC | 5V | |
| RES | D2 | 3 | | GND | GND | |
| BLK | D3 | 4 | | | | |
| VCC / GND | 3V3 / GND | | | | | |

The MPU6050 can run from 5V because GY-521 breakouts have their own 3.3 V regulator, and their SDA/SCL pull-ups go to that 3.3 V. A bare MPU6050 without a regulator must use 3V3. The LCD always uses 3V3.

This LCD has no CS pin, so `LCD_CS` is `-1` and the firmware uses SPI mode 3. D0, D5, D6 and D7 are free. If you switch to a module that has CS, wire it to D0 and set `LCD_CS 1`. Colors look inverted? Flip `LCD_INVERT`. For a 240×280 panel, set `LCD_H 280` and `LCD_OFFSET_Y 20`.

## Build & flash

```
pio run -t upload
pio device monitor
```
On boot the serial log prints `Wi-Fi MAC (register on NC State Nomad): xx:xx:...`. Register that on Nomad, then enter SSID `ncsu` (no password) on the control page. The firmware never changes the factory MAC.

## Control page (GitHub Pages)

1. Push this repo to GitHub, go to **Settings → Pages**, and set the source to the `main` branch and the `/docs` folder.
2. Open `https://<you>.github.io/<repo>/` in Chrome or Edge (Web Bluetooth and Web Serial need these browsers; on iPhone use the Bluefy app).
3. Connect over **Bluetooth** (no Wi-Fi needed) or **USB**. The first time you **Save settings**, the page stores its own URL on the device as the QR target.

The page lets you switch apps, upload a GIF, set Wi-Fi (PSK or optional WPA2-Enterprise), the countdown date, the weather location, and the display orientation.

GIFs (and PNG/JPG/WebP images) are cropped to 240×240 and re-encoded **in the browser**. Frames are dropped until the file is under about 1.9 MB, so the board does no scaling.

## Gestures (MPU6050)

| Gesture | Action |
|---|---|
| Roll right (>32°, held 120 ms) | Next app |
| Roll left | Previous app |
| Tilt toward you, back to level, toward you again (within 0.5 s) | Show QR code for the control page (20 s) |

After a switch you must return within 12° of level before the next one can fire, plus a 500 ms cooldown. Tune everything in `src/config.h`. If left and right are swapped, set `IMU_ROLL_SIGN -1`. If roll uses the wrong axis, change `IMU_ROLL_AXIS`. To see live values while tuning, uncomment the debug `printf` in `src/imu.cpp`.

## Clock screen

Weather icon and temperature along the top, a big glowing time in the middle (the colon pulses each second and a thin bar sweeps through the minute), and the date at the bottom.

Behind it is a seasonal background:
- **Winter:** flashing Christmas lights, with sparkles, or snow that piles up at the bottom and slowly melts.
- **Spring:** petals.
- **Summer:** fireflies.
- **Fall:** leaves.

Live weather overrides the season: snow appears only when it's actually snowing, and rain when it's raining. Particles fall toward real gravity, so tilting the cube makes them drift. Pick **Clock background** on the control page to force one style or turn it off. In `src/config.h`, set `CLOCK_FX 0` to disable the background, or `CLOCK_TILT_SIGN -1` if particles drift the wrong way.

## Auto concert dates

On the control page, under **Settings → Count down to**, pick an artist. The cube then counts down to that artist's next show and moves on to the following one about 6 hours after each show starts. Choose "A date I type in myself" to set a date by hand.

How it works:
- **Daily update:** `.github/workflows/concerts.yml` runs [tools/fetch_concerts.py](tools/fetch_concerts.py) every day. It looks up every artist in [concerts/artists.json](concerts/artists.json) (HYBE, JYP and YG acts; edit the file to add more) on the Ticketmaster Discovery API and writes `docs/concerts/<artist>.json`.
- **On the cube:** it downloads its artist's file hourly over Wi-Fi.

One-time setup:
1. Get a free API key at developer.ticketmaster.com: register, then copy the Consumer Key from **My Apps**.
2. In the GitHub repo, go to **Settings → Secrets and variables → Actions → New repository secret**. Name it `TM_API_KEY` and paste the key.
3. In the repo's **Actions** tab, open **Concert dates** and click **Run workflow** to fill the list straight away.

Ticketmaster covers shows sold through Ticketmaster and Live Nation, mostly in the US, Canada, Europe and Australia. For shows it misses, such as Korea and Japan dates or fan meetings, add them to [concerts/manual.json](concerts/manual.json), with times in US Eastern.

## Beam splitter

The reflection is a mirror image. Pick one of the "mirrored" orientations (4–7) on the control page until text reads correctly through the cube.

If the cube isn't centered over the active area, use **Alignment** on the control page. It shows a border and crosshair, and the arrow buttons move the whole image 1 pixel (about 0.1 mm) at a time. The value is saved on the device. The edge the image moves toward is cropped, and the opposite edge stays black. Directions are as seen through the cube, so with a rotated or mirrored orientation they won't match the panel's own axes. `LCD_SHIFT_X` / `LCD_SHIFT_Y` in `src/config.h` set the default before anything is saved.

## Adding an app

```cpp
struct MyApp : App {
  const char* name() const override { return "My App"; }
  bool draw(LGFX_Sprite& c) override { c.fillScreen(TFT_BLUE); return true; }  // return true = push frame
};
static MyApp myApp;
// add &myApp to APPS[] in src/apps/apps.cpp
```
Hooks: `begin()` (boot), `enter()`/`exit()`, `update(now)`, `draw(canvas)` (≤30 fps into a PSRAM framebuffer), `direct()` (draw straight to `lcd`, as the GIF player does). New apps show up on the control page automatically.

## Protocol (USB serial 115200 and BLE Nordic-UART, newline-delimited JSON)

`{"cmd":"info"}` · `{"cmd":"app","i":1}` · `{"cmd":"qr"}` · `{"cmd":"align"}` · `{"cmd":"shift","x":14,"y":0}` · `{"cmd":"wifi","ssid","pass","euser","eid"}` ·
`{"cmd":"cfg","lat","lon","rot","url","evn","evt":"2026-12-20T19:00"}` · `gif_begin{size}` → `gif_chunk{d:base64}`… → `gif_end`.
The device replies with `state` / `ack` / `ok` / `err` objects. Log lines never start with `{`.

## Notes

- Weather: Open-Meteo, which needs no API key. Refreshed every 15 minutes.
- Time: NTP, in US Eastern time with daylight saving.
- The TXT concert date is **not hardcoded**. Enter it on the control page; it's stored in NVS.
- Storage: custom partitions give 3 MB for the app and about 4.9 MB for LittleFS. GIFs play from PSRAM.
- NFC: not implemented. QR is the handoff method. A PN532 or ST25 on the same I2C bus could be added later.
