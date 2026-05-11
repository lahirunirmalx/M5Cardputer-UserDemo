# tools/

Helper scripts for the firmware.

## `generate_icons.py`

Renders launcher icons (R5G6B5, 56×56 big / 40×40 small) and writes the
matching C headers consumed by each app's `_Packer::getAppIcon()`.

Requires Pillow:

```bash
pip install Pillow
```

Run from the repo root:

```bash
# regenerate all icons
python3 tools/generate_icons.py

# only the calculator
python3 tools/generate_icons.py --only calc

# write a side-by-side preview PNG too
python3 tools/generate_icons.py --preview /tmp/icons.png

# dry run
python3 tools/generate_icons.py --dry-run
```

Adding a new app icon: write a `draw_<name>(im)` function and append a
tuple to the `ICONS` list at the bottom. The background color
(`0x3ce7`) and the 56/40 sizes match the rest of the icon set in
`main/apps/*/assets/`.
