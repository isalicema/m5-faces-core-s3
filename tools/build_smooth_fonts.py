"""Build local VLW alpha fonts from OFL Noto Sans CJK; retain OFL.txt with assets."""
from pathlib import Path
import hashlib,struct,json
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'assets/fonts'
chars=set(chr(c) for c in range(32,592))
for a in range(0xA1,0xF8):
 for b in range(0xA1,0xFF):
  try:chars.add(bytes([a,b]).decode('gb2312'))
  except UnicodeDecodeError:pass
chars.update('…−＋♪♥♡‹›–—‘’“”')
# Frequently played artist David Tao: U+5586 is outside GB2312.
chars.add('喆')
# Track names can use CJK presentation forms outside GB2312 (e.g. HOPERUI's
# repeated U+FE4C DOUBLE WAVY OVERLINE). Keep the original Unicode, not ASCII ~.
chars.update('〜～﹉﹊﹋﹌')
# Stable sorted glyph table; Basic Multilingual Plane is the VLW format limit.
chars=sorted(chars,key=ord)
fontpath=folder/'NotoSansCJKsc-Regular.otf'
manifest={'source':'https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf','source_sha256':hashlib.sha256(fontpath.read_bytes()).hexdigest(),'license':'OFL.txt','glyphs':len(chars),'fonts':[]}
for size in (10,12,16):
 source=folder/('NotoSansCJKsc-Bold.otf' if size==16 else 'NotoSansCJKsc-Regular.otf')
 glyphs=chars if size!=10 else sorted(set(chr(c) for c in range(32,127))|set('网易云音乐连接等待状态未知暂停播放设置'),key=ord)
 font=ImageFont.truetype(str(source),size)
 records=[];bitmaps=[];ascent=0;descent=0
 for ch in glyphs:
  x0,y0,x1,y1=font.getbbox(ch,anchor='ls');w,h=x1-x0,y1-y0
  advance=round(font.getlength(ch))
  # M5GFX VLW alpha renderer enters a do/while row loop even at h=0.
  # Store an explicit transparent pixel for blank glyphs; preserve advance.
  blank=not (w and h)
  if blank:w=h=1;x0=y0=y1=0
  mask=Image.new('L',(w,h))
  if not blank:ImageDraw.Draw(mask).text((-x0,-y0),ch,font=font,fill=255,anchor='ls')
  records.append(struct.pack('>7i',ord(ch),h,w,advance,-y0,x0,0))
  bitmaps.append(mask.tobytes())
  ascent=max(ascent,-y0);descent=max(descent,h+y0)
 data=struct.pack('>6i',len(glyphs),11,size,0,ascent,descent)+b''.join(records)+b''.join(bitmaps)
 out=folder/f'faces{size}.vlw';out.write_bytes(data)
 manifest['fonts'].append({'file':out.name,'glyphs':len(glyphs),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'size':size,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'ascent':ascent,'descent':descent})
 print(out.name,len(glyphs),len(data),'alpha levels',len(set(b''.join(bitmaps))))
(folder/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
