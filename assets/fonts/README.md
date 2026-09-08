# Embedded font assets

The source font is Noto Sans CJK SC from the Noto CJK project. Its SIL Open
Font License is retained in `OFL.txt`. `manifest.json` records source details.

The generated VLW files provide the firmware's selected Latin and CJK glyphs.
Rebuild them with:

```sh
/usr/bin/python3 tools/build_smooth_fonts.py
```

The command requires Pillow and intentionally produces only a UI-focused
subset, not complete Unicode coverage.
