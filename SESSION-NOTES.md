# dev-main build-and-flash attempt — session log (2026-05-19)

This file documents where this session left off. Pick up here next time.

## Final state on this temp branch

- **`tools/pin_components.sh`** points at the newest matched pair:
  M5GFX 0.2.21 + M5Unified 0.2.15 (both 2026-05-15), plus the older pins
  for mooncake / mooncake_log / smooth_ui_toolkit.
- **`main/hal/hal_cardputer.cpp`** has the M5.begin() patch — replaces
  the bare `new M5GFX; init()` with `M5.begin(cfg); _display = &M5.Display`
  (internal_mic / internal_spk / internal_imu disabled to avoid double-init).
- **Build succeeds.** `cardputer.bin` is 2.66 MB (35% headroom).
- **Flash succeeds** — both 0.2.0 pair and 0.2.21/0.2.15 pair write cleanly.
- **Runtime: still crashes** with `LoadProhibited` at
  `_canvas->createSprite(206, 109)` (hal_cardputer.cpp:46).

## What was tried, in order

| # | Change                                                          | Result                                       |
| - | --------------------------------------------------------------- | -------------------------------------------- |
| 1 | Restore `components/{arduino,ESP8266Audio,NeoLED}` content      | Submodule dirs no longer empty               |
| 2 | `pip install future` into IDF python venv                       | confgen.py imports work                      |
| 3 | Run in clean env (`env -i …`) so IDF picks `idf4.4_py3.8_env`   | Wrong-env kconfiglib mismatch resolved       |
| 4 | Remove `managed_components/*/.component_hash`                   | Lockfile drift cleared                       |
| 5 | Pin M5GFX → 0.2.0, M5Unified → 0.2.0 (matched Nov 8 2024)       | Compiles past API skew                       |
| 6 | Pin mooncake → aa591b3 (pre-v2 rewrite)                         | Gets past v2-API undefined errors            |
| 7 | Pin mooncake_log → 967f61b, smooth_ui_toolkit → cd77951         | Gets past C++17 / C++14 requirement errors   |
| 8 | First successful flash with 0.2.0 pair                          | Boots → crash at `_canvas->createSprite`     |
| 9 | Re-pin to M5GFX 0.2.21 + M5Unified 0.2.15 (newest tags)         | Build works, same runtime crash              |
| 10 | Apply `M5.begin()` patch in `_display_init`                    | Build works, **same runtime crash**          |

## Crash forensics

```
Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
EXCVADDR: 0x00000010
Backtrace:
  lgfx::v1::LGFXBase::width()       LGFXBase.hpp:319
  (inlined) clearClipRect()         LGFXBase.cpp:120
  (inlined) setRotation(uint8_t)    LGFXBase.cpp:62
  LGFX_Sprite::createSprite(w, h)   LGFX_Sprite.hpp:181
  HalCardputer::_display_init()     hal_cardputer.cpp:46
  HalCardputer::init()              hal_cardputer.cpp:108
  app_main                          cardputer.cpp:43
```

`_panel` field is at offset 0x10 of `LGFXBase`. `EXCVADDR=0x10` means
`this->_panel` was read from a NULL `this`. So the crash is *not*
`_panel == nullptr` on a valid Sprite; it's that **`_canvas` itself
is null** — i.e., `new LGFX_Sprite(_display)` returned null.

ESP-IDF C++ builds run with exceptions-enabled but the project may not
have a `std::new_handler` installed, so heap exhaustion → silent null
`new`. `M5.begin()` itself allocates substantially (display panel-bus
DMA, RTC, button manager); the residual contiguous heap may be too
small for the LGFX_Sprite + Panel_Sprite plus the 206×109×2 = ~45 KB
pixel buffer that `createSprite` itself allocates next.

## Hypotheses worth testing next (not done this session)

1. **PSRAM-backed sprite.** Change the createSprite call to allocate
   in PSRAM:
   ```cpp
   _canvas->createSprite(206, 109, &fg_color, true);  // last arg = use PSRAM
   ```
   The signature in M5GFX 0.2.21 is `createSprite(w, h)` though — the
   PSRAM toggle is `setPsram(true)` first, then createSprite.
2. **Heap check pre-allocation.** Print `heap_caps_get_free_size(MALLOC_CAP_DEFAULT)`
   and `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` before
   the `new LGFX_Sprite(_display)` to confirm the heap-exhaustion theory.
3. **Defer M5.begin's expensive inits.** `M5.config()` has flags like
   `internal_rtc = false`, `output_power = false`, etc. that may avoid
   the up-front allocations and free room for the Sprite.
4. **Move M5.begin() AFTER sprite allocation.** Allocate
   `_display = new M5GFX` and the Sprites first, then call
   `M5.begin()` / `_display->init()` afterward. The original code
   ordered allocation before init, which fits less heap pressure.
5. **The user's actual last-working component SHAs.** Neither the
   leftover-dir-timestamp pin (0.2.0 pair) nor the newest-tag pin
   (0.2.21 / 0.2.15) matches what was building before this session.
   `git reflog` inside `components/M5GFX/.git` and `components/M5Unified/.git`
   may show prior checkout points.

## Reset to known-good state if needed

```bash
# Drop the M5.begin patch and the newer pins, go back to original code:
git checkout main/hal/hal_cardputer.cpp
git -C components/M5GFX reset --hard 5268353       # back to develop merge
git -C components/M5Unified reset --hard 46aa9ed   # back to develop merge

# Or: discard this whole branch's work entirely:
git checkout dev-main
```

## Sibling branch — `cardputerADV-dev`

The cardputerADV-dev branch *does* build clean (pure IDF, no Arduino /
M5Unified API surface). That's where the work from earlier in the
session lives — 17 commits, all by lahiru, no co-authors. If dev-main
stays stuck, that branch is the productive path forward.
