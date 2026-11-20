# FlightRadar for the Waveshare ESP32-S3-RLCD-4.2

A self-contained radar scope for aircraft overhead. A spinning sweep "paints" each plane as it passes, over a terrain map of the area. Everything is set from a web page the device serves on the local network.

## Flash it from a Mac (first time, over USB)

1. Install **VS Code**, then the **PlatformIO IDE** extension (Extensions panel → search "PlatformIO").
2. Unzip this folder and open it in VS Code (File → Open Folder). PlatformIO downloads the ESP32 toolchain the first time. This takes a few minutes.
3. Plug the board into the Mac with a USB-C **data** cable.
4. Click the **→ Upload** arrow in the blue status bar at the bottom.
   - If the upload can't find the board, hold **BOOT**, tap **RESET**, release **BOOT**, then upload again.
5. Optional: click the plug icon (Serial Monitor) to watch the logs.

Command-line alternative: `pip install platformio`, then `pio run -t upload` from this folder.

## First-time setup (what the gift recipient sees)

1. The screen says to join the Wi-Fi **FlightRadar-Setup**. A QR code joins it automatically.
2. A setup page opens on the phone. If it doesn't, go to `192.168.4.1`. Pick the home Wi-Fi and enter its password.
3. The radar connects and guesses its location from the internet connection, so it works right away.
4. Open **http://flightradar.local** (or the IP address shown on the screen) to set the exact location, zoom, filters and so on.

## Buttons

| Button | Click | Hold |
|---|---|---|
| KEY | zoom in | 2 s: info page (address and QR code) · 10 s: factory reset |
| BOOT | zoom out | 1.5 s: labels on/off |

## Web page

- **Location:** type latitude/longitude, search an address, pick it on a map, or "Use my location". Browsers only share GPS with https pages, so this button falls back to an approximate location from your internet connection.
- **Display:** zoom (3–250 nm), sweep speed, refresh rate, layout, map style, range rings, track lines, label fields.
- **Aircraft:** type filter with *Categories* (airliners, helicopters, military…) and *Specific models* (grouped by manufacturer, each with an All button, plus wildcards like `B73*`). Also: hide planes on the ground, altitude band, airline/callsign prefixes, and always showing emergencies.
- **Alerts** (off by default): a banner and a chime for emergencies, military aircraft, helicopters, watched types, or anything passing close by.
- **Night:** the screen has no backlight, so it can't dim. Instead it either runs calm (no sweep, slow updates) or switches off between set hours.
- **System:** time zone, data source, device name, firmware upload, restart, factory reset.
- A live preview at the top mirrors the device screen.

## Wireless updates

After the first USB flash, either:
- upload `.pio/build/rlcd42/firmware.bin` from the web page (System → Firmware update), or
- run `pio run -e rlcd42-ota -t upload` with the Mac on the same network.

## Data sources

- Aircraft: [airplanes.live](https://airplanes.live) (free, non-commercial, 1 request/s), falling back to adsb.lol and adsb.fi.
- Terrain: AWS Open Data "Terrarium" elevation tiles. They're downloaded once per location and zoom level, then cached on the device.
- Approximate location: ip-api.com (used once, and only when no location has been set).

## Files

`src/` firmware · `web/` the two web pages (embedded automatically at build time) · `tools/embed_web.py` build step · `partitions.csv` 16 MB layout with two app slots for updates.
