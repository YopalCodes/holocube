# HoloCube: design brief for Claude Design

Paste this whole file into Claude Design. It contains everything needed. The code lives at https://github.com/YopalCodes/holocube (public), and the current control page is live at https://yopalcodes.github.io/holocube/.

## What you're designing for

HoloCube is a small desk gadget. A 1.3" square LCD lies face-up under a hollow **beam-splitter cube**, so whatever is on the screen appears as a floating, see-through reflection inside the cube. A motion sensor lets you control it: **tilt right** for the next app, **tilt left** for the previous one, and **bob it up and down twice** to show a QR code that opens the control website.

It has three apps: **Clock & Weather**, **GIF Player**, and **TXT Countdown**, a live countdown to the next concert by the K-pop group TXT. A companion website connects to the device over Bluetooth or USB and controls everything.

Please design three things:

1. A new **Clock & Weather screen** for the 240×240 display.
2. A new **TXT Countdown screen** for the 240×240 display.
3. A friendlier, more intuitive **control website**. My starting ideas are in part 3 below.

## Hard constraints for the two screens

- **240 × 240 pixels, square.** Design at 1× (240×240) and also show a 3× zoom for review.
- **Black is see-through.** In a beam-splitter cube, pure black (#000000) shows as empty space, and only lit pixels float in the air. Design for a **black background**, with bright elements floating on it. Large bright fills look like a glowing slab and hide the illusion. Thin, saturated lines and glowing type work best.
- **Contrast:** the reflection is dimmer than the raw screen, so text needs to be bold and large. Nothing under about 14 px tall, and avoid very thin, hairline strokes.
- **Colors:** the panel uses 16-bit color (RGB565), so subtle gradients band. Prefer flat colors, or gradients with 3–4 steps. Give every color as hex.
- **Mirroring is handled for you:** the firmware flips the image so the reflection reads correctly. Design it the way the viewer sees it.
- **Animation is possible but cheap only when it's small:** the screen redraws about 30 times a second at most. A seconds tick, a pulsing dot, or a slow sweep are fine; full-screen motion is not.
- **Implementable:** this is drawn in C++ (LovyanGFX) from shapes, text and small bitmaps. Please deliver:
  - a pixel spec: x/y position, size, font, weight and color for every element
  - any custom font as a Google Font name, which I'll convert
  - any icons as PNGs on a transparent background, at the exact pixel size used

## Screen 1: Clock & Weather

**Data available:**
- Time in US Eastern, 12-hour, with AM/PM and seconds
- Weekday, month and day
- Current temperature (°F)
- Wind speed (mph)
- Weather condition, one of: Clear, Partly cloudy, Overcast, Fog, Drizzle, Rain, Snow, Showers, Snow showers, Thunderstorm

**States to design:**
1. **Normal:** time, date and weather.
2. **No weather yet:** time is known, but the weather hasn't loaded.
3. **No time yet:** Wi-Fi isn't set up or not connected. It currently shows "--:--" and "Set Wi-Fi on control page".

**Asks:**
- The time should be the hero, readable from across a desk.
- A small **weather icon set** covering the 10 conditions above, matching the screen's style, around 32–48 px.
- A way to show seconds that feels alive without being busy, such as a ring, a tick, or a small number.

The current version is plain centered text: big time, seconds and AM/PM, date, a divider, temperature, then condition and wind.

## Screen 2: TXT Countdown

**Data available:**
- Event title (the user types it, default "TXT")
- Event date and time in Eastern
- Time remaining: days, hours, minutes, seconds

**States to design:**
1. **Counting down,** more than a day left.
2. **Final day,** under 24 hours: make it feel urgent and exciting.
3. **Show time!**, from the start until 6 hours after.
4. **Past event,** prompting the user to set the next date.
5. **No date set,** prompting "Set the concert date on the control page".
6. **Waiting for time sync,** when there's no Wi-Fi yet.

**Asks:**
- Make it feel like a fan's countdown: celebratory and a little dreamy. TXT fans often associate the group with soft blues and purples and a "dreamy, starry" mood. Use that as inspiration with an **original** visual treatment: **no official logos, album art or photos of the members.**
- Days remaining is the hero number.

The current version: the title at the top, a big day count, "days", HH:MM:SS, and the date at the bottom in small text.

## Part 3: Control website

**What it does today:** one long page:
- Connect (Bluetooth / USB)
- Live 240×240 screen preview
- App buttons
- GIF upload with a storage meter and Delete button
- Wi-Fi form, with the device's MAC address
- Settings: countdown title and date, weather latitude/longitude, display orientation (8 options, 4 of them mirrored), and the QR target URL

It only works in Chrome or Edge, because those browsers support Web Bluetooth and Web Serial. On iPhone it needs the Bluefy app.

**Users:** a student using it mostly on their phone, standing next to the cube. Every task should take a few taps and need no technical knowledge.

### Ideas to explore

These are suggestions; improve on them freely. The ones marked **(fw)** need a small firmware change, which is fine.

1. **Make the preview the centerpiece:** a big live preview of the cube's screen at the top, with the apps as tabs or swipeable cards under it. Tapping one switches the device.
2. **First-run setup wizard:** connect, then Wi-Fi, then the concert date, then the orientation check, then done. Show it only until setup is complete. After that, the page opens straight to the dashboard.
3. **Connection you never think about:** a sticky status chip that says Connected, Reconnecting or Offline, remembers the device, and reconnects automatically. Explain clearly when the browser doesn't support Bluetooth.
4. **Wi-Fi made simple (fw):**
   - scan and list nearby networks with signal bars, instead of typing the SSID
   - show the device's MAC address with a Copy button, plus a short "register on your campus network" tip
   - give plain-language troubleshooting when connecting fails
5. **GIF upload as a flow:**
   - drag-and-drop or the phone's photo picker
   - a crop/fit choice shown live on the round-cornered 240×240 preview
   - the size and storage impact shown before upload
   - progress with time remaining
   - a "Now playing" thumbnail
6. **Orientation picker by sight (fw):** instead of a dropdown of angles, show eight small thumbnails of how text will look, plus a "Show test pattern on the device" button. The user picks the one that reads correctly through the cube.
7. **Countdown editor:**
   - a friendly date and time picker clearly labelled "Eastern time"
   - live feedback such as "That's in 42 days"
   - optionally a queue of upcoming dates, so it moves on to the next show automatically **(fw)**
8. **Gesture help:** short looping animations of tilt left, tilt right and double-bob. Optionally a "practice" mode that shows the live tilt angle from the device and flashes when a gesture registers **(fw)**.
9. **Device panel:** storage used, sensor OK or not found (with a "Run diagnostic" button), firmware version, and Wi-Fi status. Kept out of the way until needed.
10. **Feel:**
   - mobile-first, with large tap targets
   - visual style coordinated with the two screens above, so it feels like one product
   - light and dark mode
   - every error says what happened and what to do next

**Please deliver:** a mobile layout, around 390 px wide, and a desktop layout for the dashboard, plus the setup wizard steps. Include empty, loading and error states for connect, GIF upload and Wi-Fi.

## Technical notes (for whoever implements the designs)

- Screen apps live in `src/apps/apps.cpp` (`ClockApp`, `CountdownApp`). Each draws into a 240×240 RGB565 sprite at up to 30 fps.
- The website is a single file, `docs/index.html`, in plain HTML/JS with no build step. It talks to the device in newline-delimited JSON over Web Bluetooth (Nordic UART service) or Web Serial. The device's state message contains: apps, the active app, Wi-Fi status, MAC, orientation (`rot`), the QR URL, latitude and longitude, the countdown title and date (`evn`, `evt`), weather (`wT`, `wWind`, `wText`), `timeOk`, the GIF size and storage figures.
- Any new website feature that needs device data should name the command it would send, so the firmware side can add it.
