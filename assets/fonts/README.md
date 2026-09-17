# Faces smooth fonts

Source: Noto Sans CJK SC Regular from https://github.com/notofonts/noto-cjk/tree/main/Sans
License: SIL Open Font License, retained in OFL.txt. Source digest is in manifest.json.
Generated bitmap derivatives are named Faces 12 / Faces 16 and are used in the local music-controller firmware.

Rebuild with `/usr/bin/python3 tools/build_smooth_fonts.py` (Pillow required).
The VLW assets contain 8-bit coverage masks, Basic Latin/Latin extensions,
GB2312 characters, and selected UI punctuation and wave/overline forms (7,986 glyphs in the 12px and 16px fonts).
This is not complete Unicode coverage: uncommon CJK, emoji and other scripts can
still fall outside the subset. Heart and volume controls are drawn as geometry.
The two embedded fonts allocate their lookup tables once, not per frame.

U+FE49–U+FE4C, U+301C/U+FF5E and a small set of common CJK characters are
explicitly included for music metadata that would otherwise fall back to an
unrelated glyph.
