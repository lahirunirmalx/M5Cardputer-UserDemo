# M5Cardputer-UserDemo-Plus

Enhanced firmware for the [M5Stack Cardputer](https://shop.m5stack.com/products/m5stack-cardputer-kit-w-m5stamps3).

Originally derived from [WuSiYu/M5Cardputer-UserDemo-Plus](https://github.com/WuSiYu/M5Cardputer-UserDemo-Plus); this fork keeps the original launcher and core apps and adds new productivity, utility, fun, and RF-tool apps with a shared, more polished UI standard (accent-green theme, rounded display panels, faux 7-segment digits, consistent footers).

---

## Hardware

- ESP32-S3 (M5StampS3)
- 1.14" IPS LCD (ST7789V2), 240×135, full-color RGB565
- 56-key QWERTY keyboard + G0 HOME button
- IR LED on GPIO 44
- microSD slot
- on-board NeoPixel
- mic + speaker

---

## Apps

### Productivity / utility

| App | What it does |
| --- | --- |
| **Calculator** | 7-segment display with formula chip, 4×5 colored keypad, decimal & sign toggle, divide-by-zero `Err` state |
| **Resistor** | 4-band resistor calculator — arrow keys to pick band, ↑/↓ to step value, 0-9 for direct entry, G/S for gold/silver, 7-seg value with `k`/`M` suffix |
| **File Manager** | Browse / view / new folder / copy / move / rename / delete on the SD card with a confirm-before-delete prompt and a context-aware footer that doubles as a clipboard badge |
| **SysInfo** | Live dashboard: uptime, free / min heap, WiFi SSID + IP + RSSI, battery, CPU info — refreshes every second |
| **Torch** | Full-screen color fill (white / R / G / B) with adjustable LCD brightness; LCD brightness restored on exit |
| **NEO LED** | Drives the on-board NeoPixel; LCD shows a large swatch + RGB readout that mirrors the live pixel color, plus a hue scrubber and R/G/B/A shortcuts |
| **Text Editor**, **REPL**, **Keyboard**, **Set Wifi**, **Wifi Scan**, **CLOCK**, **Record**, **HELLO** | inherited apps |
| **Web Radio** | inherited streaming radio with mooncake background-task handling |

### AI / chat

| App | What it does |
| --- | --- |
| **Gemini** | Chats with `gemma-3-1b-it` over the Google Generative Language API. WiFi/key status chip, color-coded transcript (`You:` / `G:` / `[system]`), Tab toggles focus to scroll history with `;` / `.`, strips markdown, 500-char reply cap. API key persisted in NVS |
| **MP3 / WinAmp** | Plays `.mp3` files off the SD root with a big 7-seg `MM:SS` elapsed-time display, play state + volume chip, scrolling track name, progress bar, 3-line playlist |

### Games

| App | What it does |
| --- | --- |
| **Snake** | Classic snake on a 25×10 grid, score + high score, R restart, speed-up on eat |
| **Tic-Tac-Toe** | 3×3 with a simple AI (win → block → center → corner). T toggles vs-AI / 2-player. Win/loss/draw counters |

### RF / tools

| App | What it does |
| --- | --- |
| **TV-B-Gone** | Universal IR power-off cycler. Implements NEC, NEC-extended, RC5, Sony SIRC (12/15/20), and Midea AC 48-bit protocols directly via RMT. ~25 TV brand power codes + the four user-supplied Midea AC codes (`0xA20DFFFFFF70` off, `0xA20FFFFFFF73` heat, `0xA212FFFFFF6E` silent on, `0xA213FFFFFF6F` silent off). S sends one, F fires the whole list with 250 ms gap |
| **BLE Pair** | BLE proximity-advert demonstrator. Broadcasts manufacturer-specific adverts (Apple Continuity / Google Fast Pair / Samsung) that nearby phones interpret as "device available to pair". 60-second auto-stop, payload-change auto-stops the current broadcast, normal 100-120 ms adv interval. **Use responsibly — affects nearby phones.** |
| **IR**, **Chat**, **Scales**, **Env** | inherited apps |

### Monitoring

| App | What it does |
| --- | --- |
| **Claude Meter** | Polls a local Claude usage HTTP service (`/usage`, `/stats`, `/token`) every 5 min from a background FreeRTOS task that survives `destroyApp()`, so the alert keeps firing while you use other apps. Seven swipable screens: summary (big 5h % + bar + reset countdown, small 7d row), 5H usage, 7D usage, Opus/Sonnet split, totals (sessions / messages / per-model output tokens / today's tally), OAuth token expiry countdown, full detail dashboard, and a settings screen for the bearer + base URL (NVS-persisted). On each refresh the NeoPixel pulses 5× in the severity color (lime <70%, orange 70-89%, red ≥90%); a beep accompanies red and can be muted with `M`. `B` exits to background (poll keeps running), `X` stops the task entirely |

---

## Improvements vs upstream WuSiYu

In addition to everything upstream, this fork:

- Adds a shared UI standard (title bar with accent chip, rounded display panel, 7-segment digits, consistent footer key hints, FONT_REPL/FONT_SMALL split)
- Rewrites Calculator, Resistor, MP3, Gemini, File Manager, NEO LED with the new standard
- Fixes a latent bug in Resistor — tolerance band was drawn with color indices 0-3 (black / brown / red / orange) instead of the real tolerance band colors (brown / red / gold / silver)
- Adds File Manager delete confirmation (was destructive on a single key press)
- Adds the new apps listed above
- Adds `tools/generate_icons.py` — a Pillow-based icon generator that emits the R5G6B5 `image_data_*` headers used by `AppIcon_t`. All new-app icons now share a 22-color flat palette extracted from the default icons (sharp pixel art, no anti-aliasing into the green background)
- Replaces several blurry / placeholder icons with sharp vector-style ones (env and scales were duplicates; both now have their own draws)
- Moves WiFi credentials and the Claude usage bearer/base URL out of source into NVS — see [Secrets & NVS provisioning](#secrets--nvs-provisioning) below
- Cleans up build warnings (deprecated `drawRightString` overloads, ignored `const`-on-return, dead `size_t < 0` checks)

---

## Currently Working On

- **Documentation & tutorial videos** for new app authors
- **Notification Center** app (placeholder in the original roadmap, not yet implemented)
- **User preferences** persistence layer that other apps can share
- More TV-B-Gone codes (a couple hundred is feasible; ~25 ship today)

---

## To-Do

- [x] Screen Off / Charging Mode — press A for sleep, B for backlight, navigation keys wake
- [x] Built-in LED Notifications via [NeoLED](https://github.com/lahirunirmalx/NeoLED)
- [x] Calculator App
- [x] Resistor Calculator App
- [x] File Manager App
- [x] Port [WinAmp](https://github.com/VolosR/M5Mp3) (MP3 player)
- [x] Port [GeminiPuter](https://github.com/nishad2m8/GeminiPuter) (Gemini chat)
- [x] Tic-Tac-Toe
- [x] Snake
- [x] Flashlight (Torch)
- [x] System Info
- [x] TV-B-Gone (with Midea AC codes)
- [x] iPhone BLE Pairing Emulation
- [x] Google/Samsung BLE Pairing Emulation
- [x] Claude usage meter (5h / 7d / Opus-Sonnet split / token expiry / live bg LED+beep alerts)
- [x] Move all WiFi / API secrets out of source into NVS + provisioning template
- [ ] Save and load user preferences
- [ ] Notification Center App

---

## Toolchain

- [ESP-IDF v4.4.6](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/index.html)
- arduino-esp32 as an ESP-IDF component (already vendored in `components/`)
- Python 3 + Pillow for icon generation (optional, only needed if you regenerate icons)

---

## Build & flash

```bash
git clone https://github.com/lahirunirmalx/M5Cardputer-UserDemo
cd M5Cardputer-UserDemo
. ~/esp/esp-idf/export.sh        # adjust to your IDF install path
idf.py build
idf.py -p /dev/ttyACM0 -b 1500000 flash monitor
```

Or use the `flash.sh` convenience script, which does build → app flash → NVS-provisioning flash → monitor in one go:

```bash
./flash.sh                # full flow
./flash.sh --no-monitor   # skip the serial monitor
./flash.sh --app-only     # skip the NVS step
```

The script auto-detects `IDF_PATH` (`$HOME/esp/esp-idf` or `…-v4.4.6`) and the IDF Python venv, so a fresh checkout usually works without edits.

### Pinned component versions

`flash.sh` runs `tools/pin_components.sh` before build to snap the vendored
component dirs back to commits known to build this codebase. The project's
apps target a specific API surface (notably mooncake v1.x — *not* the
post-Apr-2024 v2.x rewrite) and several components have drifted forward
in upstream. Pins:

| `components/<dir>`  | Pin                       | Why                                                                       |
| ------------------- | ------------------------- | ------------------------------------------------------------------------- |
| `mooncake`          | `aa591b3` (2024-03-16)    | Last v1.x: `APP_BASE` / `mcAppGetDatabase` / `destroyApp` (gone in v2).   |
| `mooncake_log`      | `967f61b` (2024-10-27)    | Last commit before tag logging used `std::string_view` (needs C++17).     |
| `smooth_ui_toolkit` | `cd77951` (2024-12-10)    | Last commit before `animate_vector*` used `constexpr auto` (needs C++14+).|
| `M5GFX`             | tag `0.2.0` (2024-11-08)  | Matched pair — later M5GFX missing `board_M5AtomS3R*` enums.              |
| `M5Unified`         | tag `0.2.0` (2024-11-08)  | Matched pair — later M5Unified calls `init_without_reset(bool)`.          |
| `arduino` + 2 more  | `.gitmodules` SHAs        | Restored via `git submodule update --init --recursive`.                   |

Manual ops:

```bash
tools/pin_components.sh --check   # report drift only, no changes
tools/pin_components.sh           # apply pins (idempotent)
```

If a component's tag/SHA is missing locally, `--check` will say so —
run `git -C components/<dir> fetch --tags` to pull it down.

---

## Secrets & NVS provisioning

WiFi credentials and the Claude usage bearer token are NOT in source — they live in NVS so the firmware can be shared safely:

| Namespace | Keys | Used by |
| --- | --- | --- |
| `wifi` | `ssid`, `pass` | HAL (loaded on `init()`), Set WiFi app, app_radio |
| `claude` | `base`, `bearer` | Claude Meter app |
| `gemini` | `apikey` | Gemini app (set via app itself) |

### Edit at runtime

- **Set WiFi** app — prefilled with the current NVS value; entering new creds saves them back through `setWifiSSID/Password`, which write-through to NVS.
- **Claude Meter** → press `E` on any screen → Settings. `T` cycles between Bearer / Base URL. Type, hit Enter, value is committed to NVS and the next fetch uses it.

### Pre-flash provisioning (recommended for clean installs)

1. Copy the template (the committed file with placeholders) to a local copy (gitignored — never committed):

   ```bash
   cp tools/nvs_keys.csv.example tools/nvs_keys.csv
   ```

2. Edit `tools/nvs_keys.csv` with your real WiFi SSID/password, Claude base URL, and bearer token.
3. Flash with `./flash.sh` — the script detects the local CSV and runs `tools/flash_nvs.sh` after the app image, which builds `nvs.bin` via IDF's `nvs_partition_gen.py` and writes it to `0x9000`.

If `tools/nvs_keys.csv` is absent, `flash.sh` just skips the NVS step and prints a hint — you can still enter creds at runtime via the apps.

> **Never commit `tools/nvs_keys.csv` or `tools/nvs.bin`** — both are in `.gitignore`. The committed `.example` template carries only placeholders.

---

## Icons

Icons are 56×56 (big) / 40×40 (small) R5G6B5 arrays under `main/apps/<app>/assets/*.h`. To regenerate them:

```bash
pip install Pillow
python3 tools/generate_icons.py
# limit to one icon:
python3 tools/generate_icons.py --only calc
# write a side-by-side preview PNG:
python3 tools/generate_icons.py --preview /tmp/icons.png
```

Adding a new icon: write a `draw_<name>(im)` function in `tools/generate_icons.py` and add an entry to the `ICONS` list at the bottom.

---

## Safety / ethics — TV-B-Gone & BLE Pair

These two apps transmit RF signals that affect other people's devices.

- **TV-B-Gone** sends IR power-off codes. Most TVs ignore it from across the room; some don't. Aim it at your own TV or a TV in a context where powering off is acceptable.
- **BLE Pair** advertises payloads that pop up "device nearby" prompts on iPhones, Pixels, and Galaxy phones in BLE range (~10 m). It has a 60-second auto-stop, a normal advertising interval, and asks for explicit Space/Enter to start, but you can still nuisance nearby strangers if you abuse it. Use it to verify your own phone's pairing UX or learn the protocol, not to harass.

If you're not authorized to broadcast on a given device or in a given space, don't.

---

## Acknowledgments

Special thanks to **WuSiYu** for the original enhancements, **Forairaaaaa** for the underlying Mooncake framework, **VolosR** for the M5Mp3 design, **nishad2m8** for GeminiPuter, **cyberwisk** for the original Cardputer Web Radio, and the broader M5Stack community.

Contributions welcome.
