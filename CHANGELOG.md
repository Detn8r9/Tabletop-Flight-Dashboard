# Changelog

Every version is in the git history and on the [Releases](../../releases) page. `firmware.bin` installs over Wi-Fi; `flightradar-full.bin` is the complete image for a USB flash.

## 1.3.0 — 2026-11-23
- **Where flights are going:** usual routes from adsbdb.com on labels, in the side panel and on the overhead card.
- **Overhead card:** when a plane passes close, a full-screen card shows the flight, the plane, its route, height and speed, plus which way to look up. A soft ping plays.
- **Smooth movement and trails:** planes glide between updates and leave a dotted trail (classic jump-on-sweep still available).
- **Today's highlights:** highest, fastest, closest and rarest plane, military flyovers and the busiest hour, on screen and on the settings page.
- **Airport board:** planes landing and taking off at the nearest big airport; press both buttons to switch.
- **Automatic updates** from this repository's releases, a watchdog that restarts the radar if it freezes, and on-screen notices when Wi-Fi or data drop out.
- **Board extras:** room temperature and humidity, battery gauge, and a clock chip that keeps time through internet outages.

## 1.2.0 — 2026-11-22
- Town names and airports (with codes like JFK, LGA) drawn on the map, from built-in GeoNames and OurAirports data. Town names can be switched off.

## 1.1.3 — 2026-11-20
- The plane counter matches the planes drawn on screen, and the settings page reloads itself after a firmware update.

## 1.1.2 — 2026-11-20
- Downloads each plane list in full before reading it (fixes "JSON IncompleteInput"), and caps requests at 100 nautical miles.

## 1.1.1 — 2026-11-20
- Flight data now comes from adsb.lol with adsb.fi as backup, after airplanes.live started refusing unregistered apps. Switches source immediately when one refuses, and shows the reason on screen.

## 1.1.0 — 2026-11-20
- Little plane and helicopter icons; a black-and-white OpenStreetMap street map instead of terrain; plain-English labels ("Delta 1234 / Boeing 737 / 35,000 ft"); feet + mph by default with knots and metric options; a Closest box in the side panel; "seen today" counter; Help tab; tidier settings.

## 1.0.0 — 2026-11-20
- The first radar: a spinning sweep over a terrain map, live planes from free community networks, a settings page with location, zoom, filters by kind of aircraft and model, alerts and night mode, and a Wi-Fi setup hotspot with QR code. Radar fills the whole screen.

