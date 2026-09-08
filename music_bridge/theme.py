"""Cover-derived stage, rendered once on the Mac for both preview and device.

Inspired by the inspected NetEase client pipeline; this is our own local
implementation, without its proprietary code or server color metadata.
"""
import colorsys
import io
import math
from PIL import Image, ImageFilter, ImageOps

# Final full-screen black veil, baked into the shared JPEG (not over the UI).
STAGE_VEIL_OPACITY = .25

DEFAULT_THEME = {"dominant": "#384454", "base": "#202a35", "scene_id": ""}


def _hls(rgb):
    return colorsys.rgb_to_hls(*(c / 255 for c in rgb))


def _rgb(h, lightness, saturation):
    return tuple(round(c * 255) for c in colorsys.hls_to_rgb(h, lightness, saturation))


def representative_color(cover):
    sample = cover.convert("RGB").resize((50, 50), Image.Resampling.BOX)
    pixels = list(sample.getdata())
    candidates = []
    for rgb in pixels:
        _, lightness, _ = _hls(rgb)
        # Absolute channel spread avoids HSL saturation's instability near white:
        # JPEG (255,255,254) has S=1, despite being visually almost white.
        near_white = lightness >= .85 and max(rgb) - min(rgb) <= 31
        if lightness > .10 and not near_white:
            candidates.append(rgb)
    # A tiny logo or compression speck should not recolor an otherwise neutral
    # cover. Fully black/white covers also fall back to their original pixels.
    if len(candidates) < len(pixels) * .02:
        candidates = pixels
    strip = Image.new("RGB", (len(candidates), 1))
    strip.putdata(candidates)
    indexed = strip.quantize(colors=16, method=Image.Quantize.MEDIANCUT)
    _, index = max(indexed.getcolors(), key=lambda item: (item[0], -item[1]))
    return tuple(indexed.getpalette()[index * 3:index * 3 + 3])


def stage_from_cover(cover):
    dominant = representative_color(cover)
    h, lightness, saturation = _hls(dominant)
    # Compress the cover's exposure into a useful stage range: a dark forest
    # still needs visible green. Continuous knots preserve true black without
    # letting an underexposed dominant swatch black out the entire scene.
    knots = ((0., .105), (.10, .22), (.35, .27), (.85, .285), (1., .30))
    for (lo, low), (hi, high) in zip(knots, knots[1:]):
        if lightness <= hi:
            stage_light = low + (high - low) * (lightness - lo) / (hi - lo)
            break
    stage_sat = min(.72, saturation * 2.1)
    # HSL lightness alone does not predict readability (yellow/green are much
    # brighter than blue). Lower the whole tone, preserving hue, for white text.
    def luminance(rgb):
        channels = [c / 255 for c in rgb]
        linear = [c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
                  for c in channels]
        return sum(w * c for w, c in zip((.2126, .7152, .0722), linear))
    while luminance(tuple(round(c * (1 - STAGE_VEIL_OPACITY))
                          for c in _rgb(h, stage_light * 1.42 + .008, stage_sat))) > .15:
        stage_light *= .95
    base = _rgb(h, stage_light, stage_sat)
    blurred = ImageOps.fit(cover.convert("RGB"), (320, 240)).filter(ImageFilter.GaussianBlur(36))
    scene = Image.new("RGB", (320, 240))
    source, pixels = blurred.load(), scene.load()
    for y in range(240):
        for x in range(320):
            # Broad cover light pools on a continuous full-screen gradient.
            local = source[x, y]
            local_light = sum(local) / (3 * 255)
            fy, fx = y / 239, x / 319
            falloff = fy * fy * (3 - 2 * fy)
            # Wide, soft upper-left reflection, never a distinct spotlight.
            sheen = math.exp(-(((fx - .24) / .8) ** 2 + ((fy - .08) / .55) ** 2))
            tone_light = stage_light * (1.34 - .58 * falloff + .08 * sheen)
            tone_light += .012 * (local_light - .5)
            tone = _rgb(h, tone_light, stage_sat)
            grain = ((x * 73 + y * 151 + x * y * 3) % 7 - 3) * .25
            pixels[x, y] = tuple(max(0, min(255, round(c + grain))) for c in tone)
    # Final compositing step: the same veil covers gradient and blurred light
    # pools. Bake it on the Mac so hardware and preview share identical pixels;
    # cover art, text and controls are drawn afterwards and stay undimmed.
    scene = Image.blend(scene, Image.new("RGB", scene.size, "black"), STAGE_VEIL_OPACITY)
    base = tuple(round(c * (1 - STAGE_VEIL_OPACITY)) for c in base)
    out = io.BytesIO()
    scene.save(out, 'JPEG', quality=88)
    return {"dominant": '#%02x%02x%02x' % dominant,
            "base": '#%02x%02x%02x' % base}, out.getvalue()
