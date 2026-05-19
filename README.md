# Cardputer ADV User Demo

User demo source code of [Cardputer ADV](https://docs.m5stack.com/en/products/sku/K132-Adv).

> **This fork:** `cardputerADV-dev` — branched from upstream `CardputerADV`.
> See [What's added in this fork](#whats-added-in-this-fork) below for the
> delta on top of stock, and [PORTING-FROM-DEV-MAIN.md](PORTING-FROM-DEV-MAIN.md)
> for the per-app porting log.

## What's added in this fork

This branch carries the apps and tooling that were originally written for
the [M5Cardputer fork on `dev-main`](https://github.com/lahirunirmalx/M5Cardputer-UserDemo/tree/dev-main),
re-ported to Cardputer-ADV's class-based mooncake app system and pure-IDF
HAL. None of the upstream CardputerADV behaviour is removed — the new
apps install alongside the existing Launcher / WiFi / Clock / IMU /
LoRa / GPS / StringIR-Toolkit set.

### Ported apps (9)

| App         | Source dir                       | Notes                                                            |
| ----------- | -------------------------------- | ---------------------------------------------------------------- |
| Notepad     | `main/apps/app_texteditor/`      | Appends lines to `/sdcard/note.txt`. Ctrl+Bksp deletes the file. |
| Files       | `main/apps/app_files/`           | SD browser with copy/move/rename/delete + delete confirm dialog. |
| Calculator  | `main/apps/app_calculator/`      | Faux-7-seg display, on-screen keypad, divide-by-zero state.      |
| Resistor    | `main/apps/app_resistor/`        | 4-band color-code → SI-prefixed value with ±tolerance.           |
| Torch       | `main/apps/app_torch/`           | Full-screen flashlight; white/red/green/blue, brightness ramp.   |
| SysInfo     | `main/apps/app_sysinfo/`         | Live uptime, heap, WiFi SSID/IP/RSSI, battery, CPU. 1 Hz refresh.|
| Snake       | `main/apps/app_snake/`           | 25×10 grid; arrows steer, R restart, speed ramps with score.     |
| Tic-Tac-Toe | `main/apps/app_tictactoe/`       | Vs simple AI or 2-player; persistent session score chip.         |
| Claude Meter| `main/apps/app_claudemeter/`     | Background HTTP poll of `/usage`, `/stats`, `/token` every 5 min.|

Each app was committed individually — `git log --oneline cardputerADV-dev ^CardputerADV`
shows the per-app trail.

### Shared utility

- **`main/apps/utils/seven_seg/`** — pixel-identical 7-segment digit
  renderer shared by Calculator and Resistor. Drop-in for any future
  large-number display.

### Tooling

- **`tools/nvs_keys.csv.example`** — committed template for the device's
  NVS partition (WiFi creds + Claude API). Copy → edit → `flash_nvs.sh`.
  See [Provisioning credentials via NVS](#provisioning-credentials-via-nvs).
- **`tools/flash_nvs.sh`** — generates `tools/nvs.bin` from
  `tools/nvs_keys.csv` and writes it to flash at `0x9000` (`0x4000` bytes
  per [partitions.csv](partitions.csv)). Does not touch the app image.
- **`tools/generate_icons.py`** — host-side Pillow script that draws the
  56×56 / 40×40 launcher icons for the ported apps and emits R5G6B5
  `image_data_*` headers under each app's `assets/`. See
  [Regenerating icons](#regenerating-icons).

### Build-system / config

- **`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`** added to `sdkconfig.defaults`
  so `esp_http_client` can verify HTTPS endpoints against the bundled
  CA root store. Needed for Claude Meter; harmless for HTTP-only use.
- **`.gitignore`** entries for `tools/nvs_keys.csv`, `tools/nvs.bin`,
  `build_alpha.zip`, and `__pycache__/` so secrets and bulky artefacts
  can't slip into a commit.

### Apps deliberately *not* ported

Five dev-main apps depend on peripherals or Arduino-only libraries that
don't exist on Cardputer-ADV's pure-IDF stack; each is documented with
a revival plan in [PORTING-FROM-DEV-MAIN.md](PORTING-FROM-DEV-MAIN.md):

- **AppLed** — requires the M5Cardputer WS2812 NeoPixel hardware.
- **AppMp3** — needs the Arduino `ESP8266Audio` library.
- **AppGemini** — uses Arduino `WiFi.h` / `HTTPClient.h`.
- **AppTvbgone** — uses legacy RMT v4 + Cardputer-specific IR GPIO.
- **AppBlePair** — uses Arduino `BLEDevice.h` + raw advertising; the
  CardputerADV BLE layer is HID-only.

## Provisioning credentials via NVS

Both the WiFi connection and the Claude usage API are read from NVS,
so no secrets ever enter source files. Workflow:

```bash
cp tools/nvs_keys.csv.example tools/nvs_keys.csv     # gitignored copy
$EDITOR tools/nvs_keys.csv                           # fill in real values
. $IDF_PATH/export.sh                                # idf env on PATH
tools/flash_nvs.sh /dev/ttyACM0                      # generate + flash NVS only
```

Re-flashing the app image (`idf.py flash`) does **not** clear NVS.
Only `idf.py erase-flash` wipes the device back to factory state.

Namespaces and keys actually read by the firmware:

| Namespace   | Keys                          | Consumer                                  |
| ----------- | ----------------------------- | ----------------------------------------- |
| `cardputer` | `wifi_ssid`, `wifi_password`  | `hal.cpp` Settings + `app_set_wifi`       |
| `claude`    | `base`, `bearer`              | `app_claudemeter` (direct `nvs_open`)     |

The Settings screen inside Claude Meter (E key) also writes through to
the `claude` namespace, so over-the-air re-provisioning works without
needing a host machine.

## Regenerating icons

```bash
pip install pillow                              # one-time host requirement
python3 tools/generate_icons.py                 # regen all ported-app icons
python3 tools/generate_icons.py --only calc     # one icon at a time
python3 tools/generate_icons.py --preview /tmp/icons.png    # write a 4x preview
```

Headers land in `main/apps/<dir>/assets/<name>_big.h` and `<name>_small.h`,
overwriting any existing files. To add a future port's icon, write a
new `draw_<name>(im)` function in the script and add a tuple to the
`ICONS` list — the draw functions for the dev-main apps that aren't
ported yet (MP3, Gemini, LED, TVBGone, BlePair, Env, Scales) are
already in the script so reactivating them is one line.

---

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.4.2](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/index.html)
is the target. ESP-IDF 5.3.x also builds clean — this fork was last
verified on 5.3.1 via the PlatformIO-bundled `framework-espidf`.

### Build

```bash
idf.py set-target esp32s3
idf.py build
```

Expected output: `build/cardputer-adv.bin` ~2.9 MB, leaving ~30 %
headroom in the 4 MB `factory` partition.

### Flash

```bash
idf.py flash                       # full image — app + bootloader + partition table
tools/flash_nvs.sh /dev/ttyACM0    # NVS only — preserves the app image
```

## Acknowledgments

This project references the following open-source libraries and resources:

- https://github.com/adafruit/Adafruit_TCA8418
- https://github.com/m5stack/M5Unified.git
- https://github.com/pikasTech/PikaPython
- https://github.com/jgromes/RadioLib
- https://github.com/raysan5/raylib
- https://github.com/mikalhart/TinyGPSPlus
- https://github.com/m5stack/M5GFX.git
- https://github.com/Forairaaaaa/mooncake_log
- https://github.com/hhuysqt/esp32s3-keyboard
- https://github.com/78/xiaozhi-esp32
- https://github.com/Forairaaaaa/mooncake
- https://github.com/Forairaaaaa/smooth_ui_toolkit
