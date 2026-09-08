import base64
import io
import unittest
from unittest import mock
from PIL import Image, ImageDraw
from music_bridge.theme import stage_from_cover, representative_color, DEFAULT_THEME
from music_bridge.backend import MusicBackend

class ThemeTests(unittest.TestCase):
    def test_area_majority_beats_small_bright_accent(self):
        cover=Image.new('RGB',(144,144),(30,70,140))
        ImageDraw.Draw(cover).rectangle((0,0,29,143),fill=(255,240,0))
        theme,jpeg=stage_from_cover(cover)
        self.assertEqual(theme['dominant'],'#1e468c')
        scene=Image.open(io.BytesIO(jpeg))
        self.assertEqual(scene.size,(320,240))
        self.assertLess(len(jpeg),40000)
        self.assert_white_text_contrast(scene)

    def assert_white_text_contrast(self, scene):
        def linear(c):
            c /= 255
            return c / 12.92 if c <= .04045 else ((c + .055) / 1.055) ** 2.4
        brightest = max(sum(w * linear(c) for w, c in zip((.2126,.7152,.0722), pixel))
                        for pixel in scene.getdata())
        self.assertGreaterEqual(1.05 / (brightest + .05), 4.5)

    def test_white_and_jpeg_off_white_do_not_hide_blue(self):
        for white in [(255,255,255),(255,255,254),(250,252,253)]:
            cover = Image.new('RGB',(100,100),white)
            ImageDraw.Draw(cover).rectangle((0,0,14,99),fill=(40,80,160))
            self.assertEqual(representative_color(cover),(40,80,160))

    def test_neutral_covers_remain_neutral_and_black_is_darker(self):
        bases = []
        for level in (0,128,255):
            theme,jpeg = stage_from_cover(Image.new('RGB',(100,100),(level,)*3))
            base = tuple(int(theme['base'][i:i+2],16) for i in (1,3,5))
            self.assertEqual(len(set(base)),1)
            bases.append(base[0])
            self.assert_white_text_contrast(Image.open(io.BytesIO(jpeg)))
        self.assertEqual(bases,sorted(set(bases)))

    def test_mid_gray_stays_eligible_and_tiny_logo_cannot_tint_white(self):
        cover = Image.new('RGB',(100,100),(128,128,128))
        draw = ImageDraw.Draw(cover)
        draw.rectangle((0,0,24,99),fill='white')
        draw.rectangle((25,0,39,99),fill=(40,80,160))
        self.assertEqual(representative_color(cover),(128,128,128))
        cover = Image.new('RGB',(100,100),'white')
        ImageDraw.Draw(cover).rectangle((0,0,9,9),fill='blue')
        self.assertEqual(representative_color(cover),(255,255,255))

    def test_tonal_mapping_keeps_text_legible_across_palette(self):
        import colorsys
        for h in range(12):
            for lightness in (.15,.5,.8):
                rgb = tuple(round(c*255) for c in colorsys.hls_to_rgb(h/12,lightness,.8))
                _,jpeg = stage_from_cover(Image.new('RGB',(100,100),rgb))
                self.assert_white_text_contrast(Image.open(io.BytesIO(jpeg)))

    def test_different_covers_create_different_stages(self):
        warm,a=stage_from_cover(Image.new('RGB',(144,144),(200,120,30)))
        cool,b=stage_from_cover(Image.new('RGB',(144,144),(30,120,200)))
        self.assertNotEqual(a,b);self.assertNotEqual(warm['base'],cool['base'])
        self.assertEqual(stage_from_cover(Image.new('RGB',(144,144),(200,120,30)))[1],a)

    def test_scene_cached_and_cleared_with_track(self):
        backend=MusicBackend()
        out=io.BytesIO();Image.new('RGB',(144,144),'blue').save(out,'PNG')
        raw=dict(bundleIdentifier='com.netease.163music',title='one',playing=True,
                 artworkData=base64.b64encode(out.getvalue()).decode())
        with mock.patch.object(backend,'call',return_value=raw),mock.patch.object(backend,'native',return_value={}):
            backend.refresh(); first=backend.snapshot()['theme'];self.assertTrue(first['scene_id'])
            with mock.patch('music_bridge.backend.stage_from_cover') as build:
                backend.refresh();build.assert_not_called()
            self.assertIn(first['scene_id'],backend.artworks)
            raw.pop('artworkData');raw['title']='two';backend.refresh()
            self.assertEqual(backend.snapshot()['theme'],DEFAULT_THEME)

if __name__=='__main__':unittest.main()
