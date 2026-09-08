import struct,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class SmoothFontTests(unittest.TestCase):
 def test_assets_have_complete_alpha_glyphs_and_fit(self):
  for size in (10,12,16):
   data=(ROOT/f'assets/fonts/faces{size}.vlw').read_bytes()
   count,version,points,_,asc,desc=struct.unpack_from('>6i',data)
   self.assertEqual(version,11);self.assertEqual(points,size)
   pos=24+28*count;glyphs={}
   for i in range(count):
    code,h,w,advance,dy,dx,_=struct.unpack_from('>7i',data,24+28*i)
    self.assertGreaterEqual(asc,dy);self.assertGreaterEqual(desc,h-dy)
    self.assertGreater(w,0);self.assertGreater(h,0)
    self.assertLess(w,256);self.assertLess(h,256)
    glyphs[chr(code)]=data[pos:pos+w*h];pos+=w*h
   self.assertEqual(pos,len(data))
   self.assertTrue(glyphs[" "]);self.assertFalse(any(glyphs[" "]))
   for ch in ('网易云音乐PAUSED0123456789' if size==10 else '网易云音乐喜欢连接暂停播放设置音量中文Oasiséèà'):
    self.assertIn(ch,glyphs)
    self.assertTrue(any(0<a<255 for a in glyphs[ch]),ch)
   self.assertGreater(len(glyphs),100 if size==10 else 7000)
if __name__=='__main__':unittest.main()
