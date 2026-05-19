# Porting features from `dev-main` → `cardputerADV-dev`

This branch (`cardputerADV-dev`) is forked from `CardputerADV` (Cardputer-ADV
hardware target). The `dev-main` branch targets the **original M5Cardputer**
and uses an incompatible app system and HAL. Features cannot be merged or
cherry-picked wholesale — each must be hand-ported.

This file is the working checklist. Tick items as they land and link the
commit hash.

---

## Why this isn't a `git merge`

`dev-main` and `CardputerADV` share an early merge base (`7d99ee4`) but have
diverged into two distinct platform forks. As of this branch's creation,
**945 files differ** and the differences span the platform layer, not just
features:

| Layer            | `dev-main`                                        | `CardputerADV`                                        |
| ---------------- | ------------------------------------------------- | ----------------------------------------------------- |
| Target board     | M5Cardputer (original)                            | Cardputer-ADV                                         |
| Entry point      | `main/cardputer.cpp`                              | `main/main.cpp`                                       |
| App registration | `mooncake.installApp(new APPS::AppXxx_Packer)`    | `GetMooncake().installApp(std::make_unique<AppXxx>())`|
| App base class   | Forairaaaaa packer pattern                        | mooncake class app                                    |
| Keyboard         | direct cardputer matrix scan                      | TCA8418 I²C expander                                  |
| Audio            | `Mic_Class` + `Speaker_Class` (M5Unified-derived) | (separate path)                                       |
| Settings/NVS     | `getWifiSSID()` HAL accessor, ns=`wifi`           | `Settings("cardputer")`, keys `wifi_ssid`/`wifi_password` |
| Partition `nvs`  | `0x9000` size `0x5000`                            | `0x9000` size `0x4000`                                |

A blind `git merge dev-main` would produce thousands of conflicts across
files that aren't really the "same file" in any semantic sense.

---

## Inventory of `dev-main` work to consider porting

Generated from `git log CardputerADV..dev-main` at branch creation. Items
listed roughly oldest-first (the order they were built on dev-main).

### New apps (each is its own port, each its own commit)

| dev-main commit | Feature                              | Port status | Notes / blockers                                                                                              |
| --------------- | ------------------------------------ | ----------- | ------------------------------------------------------------------------------------------------------------- |
| `326739f`       | 4 new apps + 6 UI redesigns          | TODO        | Bundle commit — break into per-app ports.                                                                     |
| `c4d0331`       | Resistor: arrow-key band navigation  | TODO        | App doesn't exist here yet; depends on Resistor app being ported first.                                       |
| `b541352`       | Files: delete confirm + clipboard chip | TODO      | App `app_files` doesn't exist on CardputerADV.                                                                |
| `1998cc7`       | TV-B-Gone app with Midea AC codes    | TODO        | Uses IR transmit. CardputerADV has IR (`hal/utils/ir_nec/`) — wire codes to that encoder.                     |
| `7dd2f92`       | BLE pairing-prompt demonstrator      | TODO        | CardputerADV already has BLE HID (`hal/utils/ble_hid_device/`) — adapt to those primitives.                   |
| `8d4deb0`       | TV-B-Gone WORLD_IR_CODES (143 NA + 140 EU) | TODO  | Depends on `1998cc7` port landing first.                                                                       |
| `4959840`       | Shared 7-segment renderer in utils/  | TODO        | Pure UI utility — port to `main/apps/utils/` then rebase Resistor/Timer/etc. on it.                            |
| `47f7677`       | Claude Meter app (usage dashboard)   | TODO        | Background polling task — port WiFi/HTTPS call + UI; settings keys live in CardputerADV `cardputer` NVS ns.   |
| `822e6b5`       | WiFi + Claude creds → NVS            | TODO        | CardputerADV already keeps WiFi in NVS (`Settings("cardputer")`). Add "claude" namespace if Claude Meter ported. |
| `70657bf`       | BLE pair: dedicated spam task + rotating MAC | TODO | Depends on `7dd2f92` port landing first.                                                                       |

### Small enhancements to existing-on-CardputerADV apps

These map to apps that *do* exist on CardputerADV (`app_chat`, `app_record`,
`app_repl`, `app_set_wifi`, `app_keyboard`, `app_wifi_scan`). Each needs a
look at the CardputerADV class-based version and a re-implementation, not a
diff apply.

| dev-main commit | Change                                                        | Port status |
| --------------- | ------------------------------------------------------------- | ----------- |
| `024dc9a`       | Gemini app: switch to `gemini-2.0-flash`                      | N/A — no Gemini app on CardputerADV (yet) |
| `ac5ffd6`       | Files: format mtime as `YYYY-MM-DD HH:MM`                     | N/A — no Files app on CardputerADV (yet)  |

### HAL / keyboard changes that **don't apply**

| dev-main commit | Change                                                          | Why skipped                                                  |
| --------------- | --------------------------------------------------------------- | ------------------------------------------------------------ |
| `9c1db8e`       | keys: cardputer-native fallbacks for arrow navigation           | Cardputer matrix layer doesn't exist on Cardputer-ADV (TCA8418). |
| `a733e87`       | neoled: bump submodule + app_led teardown                       | NeoLED submodule + `app_led` don't exist on CardputerADV.    |

### Tooling / housekeeping

| dev-main commit | Change                                | Port status                                                            |
| --------------- | ------------------------------------- | ---------------------------------------------------------------------- |
| `53f2fe2`       | gitignore `build_alpha.zip` + NVS CSV | **Ported** in commit `50f55b8` on this branch (adapted to CardputerADV's gitignore). |
| `ccd437f`       | Icons: flat palette + regenerate      | Tooling exists in `tools/generate_icons.py` on dev-main; can port if any CardputerADV app needs flat-style icons. |
| `4efc420`, `7f481b5` | README rewrites                  | CardputerADV has its own README; merge selected sections if useful.    |

### Bulk / catch-up commits

| dev-main commit | Change                       | Notes                                                              |
| --------------- | ---------------------------- | ------------------------------------------------------------------ |
| `0693739`       | "push pending development"   | Inspect contents — likely overlaps with subsequent named commits.  |
| `db209c7`       | "update readme file"         | README only.                                                       |
| `18c8d61`       | Screen-off + WiFi retry loop | The HAL split is different; reimplement against CardputerADV HAL.  |

---

## Per-app port recipe

For each app you bring over from dev-main:

1. **Copy the source dir** `main/apps/app_<name>/` from a `dev-main` checkout
   (or `git show dev-main:path/to/file`).

2. **Rewrite the entry class** from packer pattern to mooncake class:

   ```cpp
   // dev-main:
   namespace APPS {
   class AppXxx_Packer : public mooncake::AppPacker { ... };
   } // namespace APPS

   // CardputerADV:
   class AppXxx : public mooncake::AppAbility {
   public:
       AppXxx() { setAppInfo().name = "AppXxx"; }
       void onCreate() override   { /* ... */ }
       void onOpen() override     { /* ... */ }
       void onRunning() override  { /* ... */ }
       void onClose() override    { /* ... */ }
   };
   ```

3. **Swap HAL calls**. Most cardputer-specific HAL methods (e.g.
   `_data.hal->getWifiSSID()`) map to `GetHAL().getSettings().GetString("wifi_ssid", "")`.
   See `main/hal/hal.h` on this branch for the available HAL surface.

4. **Adapt input handling.** dev-main reads `_data.keyboard->isPressed(KEY_XXX)`;
   CardputerADV apps consume mooncake/M5Unified key events via the launcher's
   input bus — pattern from any existing CardputerADV app (e.g., `app_keyboard`).

5. **Add include and register**:
   - Append `#include "app_<name>/app_<name>.h"` to `main/apps/apps.h`.
   - Append `GetMooncake().installApp(std::make_unique<App<Name>>());` to
     `main/main.cpp` install block.

6. **Add assets**. Icon header files (`*_big.h`, `*_small.h`) can be carried
   over as-is — they're just byte arrays. Port `tools/generate_icons.py` from
   dev-main only if you need to regenerate.

7. **Build, fix, flash, smoke-test** — one app per commit. Commit message:
   `port: <app_name> from dev-main (ref: <dev-main commit hash>)`.

---

## What's been ported so far

Branch infrastructure:
- `50f55b8` chore: gitignore NVS credentials, build_alpha.zip, and `__pycache__`
- `88fdfac` docs: this porting checklist

Apps (one commit per app, in user-requested order):
- `0f00e54` port: AppTextEditor (Notepad) — SD-backed line editor
- `e156e0e` port: AppFilesManager — SD browser with copy/move/rename/delete
- `4ed5e4d` port: AppCalculator + shared `seven_seg` utility
- `217d450` port: AppResistor — 4-band color-code calculator
- `2110942` port: AppTorch — full-screen flashlight, brightness/color cycle
- `671994e` port: AppSysinfo — heap, WiFi, IP, RSSI, battery, CPU
- `b04d7db` port: AppSnake — 25×10 grid game
- `fd882e6` port: AppTicTacToe — vs simple AI or 2-player
- port: AppClaudeMeter — usage dashboard with background polling. HTTP
  layer rewritten on top of `esp_http_client` (bundled CA roots enabled
  via `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` for HTTPS). NeoLED blink-on-
  refresh dropped (no equivalent hardware); 880 Hz danger-severity beep
  kept via `M5.Speaker` / `audio::play_tone()`, suppressible with M.
  All 8 screens preserved, NVS namespace and key names unchanged so
  existing dev-main provisioning carries over.

Pixel layouts, key bindings, and behavior preserved; HAL/keyboard/audio
adapted to CardputerADV idioms. All eight ports compile against
`<mooncake.h>`/`<hal.h>`/`<apps/utils/*>` and use the signal-driven
`Keyboard::onKeyEvent` model. **Not built locally** on this host (IDF
Python venv is missing pinned packages); the user is expected to build
on their own host where the IDF env is configured. Each commit is
self-contained — revert any single port without affecting the rest.

## Deliberately skipped on this branch

These dev-main apps were left out of the port because they depend on
peripherals, frameworks, or low-level drivers that are not available
on Cardputer-ADV's pure-IDF stack. Each would be a rewrite rather than
a port, so they are documented here for follow-up rather than turned
into half-broken stubs.

- **AppLed** — needs the M5Cardputer WS2812 NeoPixel + the `neoled`
  submodule driving it via `driver/rmt.h`. Cardputer-ADV has no
  equivalent user-controllable RGB LED. To revive: design a new app
  against whatever LED HAL primitive a future Cardputer-ADV revision
  exposes.

- **AppMp3** — pulls in the Arduino `ESP8266Audio` library
  (`<AudioOutput.h>`, `<AudioGeneratorMP3.h>`, `<AudioFileSourceBuffer.h>`)
  for MP3 decoding + M5Speaker output. Cardputer-ADV is pure ESP-IDF,
  no Arduino-as-component. To revive: replace the audio decoder with
  an IDF-native path (Espressif's audio component or a hand-rolled
  libmad / minimp3 integration), then re-implement the playback task
  on top of `M5.Speaker`.

- **AppGemini** — uses Arduino `<WiFi.h>` / `<HTTPClient.h>` /
  `<WiFiClientSecure.h>` to hit the Google Gemini REST endpoint over
  HTTPS. Cardputer-ADV has no Arduino layer. To revive: rewrite the
  HTTP layer on top of `esp_http_client` / `esp_tls`, keep the
  request/response parsing logic, and store the API key under
  `Settings("gemini")` to match the CardputerADV NVS convention.

- **AppTvbgone** — uses the **legacy** RMT driver
  (`driver/rmt.h`, `rmt_item32_t`, `rmt_set_tx_carrier`) to emit
  arbitrary-timed IR pulse trains at code-specific carrier frequencies
  (36/38/40/56 kHz), and the dev-main version is hard-wired to
  `GPIO 44`. Cardputer-ADV has the **new** RMT driver
  (`driver/rmt_tx.h`, custom encoders) exposed via
  `main/hal/utils/ir_nec/`, which currently only emits NEC frames. To
  revive: write a generic "raw pulse-train" encoder against the new
  RMT API, refactor `world_ir_codes.h` to emit per-code carrier setup,
  and route through `GetHAL().irSend()` (or a sibling) with the
  Cardputer-ADV IR GPIO.

- **AppBlePair** — uses Arduino `<BLEDevice.h>` / `<BLEAdvertising.h>`
  to broadcast manufacturer-specific BLE adverts with rotating MAC
  addresses. Cardputer-ADV's BLE layer lives in
  `main/hal/utils/ble_hid_device/` and targets the HID profile only;
  the raw advertising primitives the dev-main app needs are not
  exposed and the stack mixes Bluedroid + NimBLE conditionals that
  diverge from dev-main's Arduino-BLE setup. To revive: implement
  against `esp_ble_gap_config_adv_data_raw()` / `esp_ble_gap_start_advertising()`
  on Bluedroid, or the NimBLE GAP equivalents, and re-port the
  payload tables verbatim.

## Tally

User-requested order: TextEditor, Led, FilesManager, Mp3, Calculator,
Resistor, Gemini, Torch, Sysinfo, Snake, TicTacToe, Tvbgone, BlePair,
ClaudeMeter — 14 apps.

- Ported: 9 (TextEditor, FilesManager, Calculator, Resistor, Torch,
  Sysinfo, Snake, TicTacToe, ClaudeMeter).
- Skipped: 5 (Led, Mp3, Gemini, Tvbgone, BlePair) — see rationale above.

---

## What's deliberately **not** being ported

- The dev-main launcher (`main/apps/launcher/`) — CardputerADV uses
  `main/apps/app_launcher/` with a different architecture.
- The dev-main HAL (`main/hal/hal_cardputer.{cpp,h}`,
  `main/hal/mic/`, `main/hal/speaker/`, `main/hal/sdcard/`,
  `main/hal/keyboard/keyboard.cpp` old version) — Cardputer-ADV's hardware
  doesn't have the same peripherals.
- `main/cardputer.cpp` entry — CardputerADV uses `main/main.cpp`.
- `sdkconfig` (full file) — CardputerADV uses `sdkconfig.defaults` and
  regenerates `sdkconfig` per build.
