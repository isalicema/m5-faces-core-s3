"""Build native-size paper stages from the self-contained local preview.

The preview carries the 4x artwork so its browser rendering and the embedded
firmware assets are generated from one public source.  No host-system fonts or
private source images are required.
"""
from base64 import b64decode
from io import BytesIO
from pathlib import Path
import re

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HTML = (ROOT / "music_bridge" / "preview.html").read_text()


def image_data(selector):
    match = re.search(selector + r'.*?data:image/png;base64,([A-Za-z0-9+/=]+)', HTML, re.S)
    if not match:
        raise RuntimeError(f"preview asset not found: {selector}")
    return b64decode(match.group(1))


def write_stage(data, preview_name, native_name):
    preview = Image.open(BytesIO(data)).convert("RGB")
    if preview.size != (1280, 960):
        raise RuntimeError(f"expected a 4x 320x240 stage, got {preview.size}")
    preview.save(ROOT / "assets" / preview_name, optimize=True)
    preview.resize((320, 240), Image.Resampling.LANCZOS).save(ROOT / "assets" / native_name, optimize=True)


write_stage(image_data(r'--home-art:url\("'), "home-stage-preview.png", "home-stage.png")
for station in range(3):
    write_stage(
        image_data(rf'\.radio-view\[data-station="{station}"\]'),
        f"radio-stage-{station}-preview.png",
        f"radio-stage-{station}.png",
    )
print("Generated Home and Radio paper stages from music_bridge/preview.html")
