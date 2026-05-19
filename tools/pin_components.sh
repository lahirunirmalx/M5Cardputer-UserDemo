#!/usr/bin/env bash
# Reset every vendored component dir to the exact commit / tag known to
# build dev-main as of 2026-05-19. Run this whenever a fresh `git pull`
# inside a component (or someone's IDE auto-fetch) has bumped one of
# them past the API the project code targets.
#
# What this script does:
#
#   1. For each non-submodule vendored repo in components/ — reset it
#      to a recorded SHA / tag and `git clean -fd` any files added in
#      later commits.
#   2. For the three real git submodules (arduino, ESP8266Audio,
#      NeoLED) — run `git submodule update --init --recursive` so the
#      .gitmodules-pinned SHAs are restored.
#
# Why these specific pins:
#
#   - mooncake aa591b3 (Mar 2024) — last commit where APP_BASE /
#     APP_PACKER_BASE / mcAppGetDatabase / destroyApp / std::string
#     getAppName() all coexist. Later commits replaced these with the
#     v2.x ability/manager API the dev-main code can't compile against.
#   - mooncake_log 967f61b (Oct 2024) — last commit before tag logging
#     was re-introduced using std::string_view, which needs C++17
#     while this project builds at -std=gnu++11.
#   - smooth_ui_toolkit cd77951 (Dec 2024) — last commit before
#     animate_vector* gained constexpr auto returns (needs C++14+).
#   - M5GFX 0.2.21 (May 15 2026) + M5Unified 0.2.15 (May 15 2026) —
#     newest matched pair, released same day. **Compiles clean and the
#     image flashes successfully; however a runtime LoadProhibited
#     panic still occurs inside HalCardputer::_display_init() at the
#     `_canvas->createSprite(206, 109)` line (see SESSION-NOTES.md).
#     The 0.2.0 pair from Nov 8 2024 was tried first — it compiled
#     and flashed but crashed identically.
#
# Usage:
#   tools/pin_components.sh        # apply pins, idempotent
#   tools/pin_components.sh --check    # print drift only, no changes
#
# Hooks: this is called from flash.sh before `idf.py build` so a fresh
# checkout self-heals before compile. Run it standalone if you want to
# inspect the resulting state without flashing.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
cd "$REPO"

# component_dir : pin (commit SHA or tag)
PINS=(
    "components/mooncake:aa591b32f37d5aed1e77dd6e4f44d2d066c98ada"
    "components/mooncake_log:967f61b8ccd6d708e7b2f3b893e0eec4d8cc9c0c"
    "components/smooth_ui_toolkit:cd779515b52780d010df26078093b71754988e27"
    "components/M5GFX:0.2.0"
    "components/M5Unified:0.2.0"
)

check_only=0
case "${1:-}" in
    --check) check_only=1 ;;
    -h|--help) sed -n '1,/^set -/p' "$0" | sed -n 's/^# \{0,1\}//p'; exit 0 ;;
    "") ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
esac

drift=0
for entry in "${PINS[@]}"; do
    dir="${entry%%:*}"
    pin="${entry#*:}"

    if [[ ! -d "$dir/.git" ]]; then
        echo "WARNING: $dir is not a git repo (run 'python3 fetch_repos.py' or" >&2
        echo "         restore the component manually), skipping pin." >&2
        drift=1
        continue
    fi

    current="$(git -C "$dir" rev-parse HEAD)"
    pinned="$(git -C "$dir" rev-parse "$pin^{commit}" 2>/dev/null || true)"

    if [[ -z "$pinned" ]]; then
        echo "ERROR: $dir has no commit/tag '$pin' locally — fetch first:" >&2
        echo "       git -C $dir fetch --tags" >&2
        drift=1
        continue
    fi

    if [[ "$current" == "$pinned" ]]; then
        printf "  %-30s OK   %s\n" "$dir" "$pin"
        continue
    fi

    printf "  %-30s DRIFT %s -> %s (%s)\n" "$dir" "${current:0:10}" "${pinned:0:10}" "$pin"
    drift=1

    if (( check_only )); then
        continue
    fi

    git -C "$dir" reset --hard "$pinned" >/dev/null
    git -C "$dir" clean -fd >/dev/null
done

if (( check_only )); then
    if (( drift )); then
        echo
        echo "Drift detected. Run \`$0\` (no --check) to fix." >&2
        exit 1
    fi
    echo
    echo "All vendored components on pinned SHAs."
    exit 0
fi

# Submodules: arduino, ESP8266Audio, NeoLED. .gitmodules already records
# the right SHAs, so update --init is enough.
echo
echo "Refreshing git submodules to recorded SHAs..."
git submodule update --init --recursive

echo
echo "Done. Components pinned to dev-main's last-known-good versions."
