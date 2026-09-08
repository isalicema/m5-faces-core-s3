"""Generate the shared Home art, browser layout and LCD geometry from one manifest."""
from pathlib import Path
from PIL import Image, ImageDraw
import math, base64, json, re
root = Path(__file__).resolve().parents[1]
layout = json.loads((root/'ui/home-layout.json').read_text())
S = 4
im = Image.new('RGB', (320*S, 240*S)); px = im.load()
for y in range(240*S):
 for x in range(320*S):
  glow = math.exp(-((x/S-265)/220)**2-((y/S-20)/170)**2)
  px[x,y] = (round(20+5*glow), round(25+7*glow), round(26+6*glow))
d = ImageDraw.Draw(im)
def box(b): return tuple(round(v*S) for v in b)
def rr(b,r,fill,outline=None,width=1): d.rounded_rectangle(box(b),round(r*S),fill=fill,outline=outline,width=max(1,round(width*S)))
def ellipse(b,fill=None,outline=None,width=1): d.ellipse(box(b),fill=fill,outline=outline,width=max(1,round(width*S)))
def line(b,fill,width=1): d.line(box(b),fill=fill,width=max(1,round(width*S)))
# Quiet mineral panels. The first app gets a larger, luminous record sleeve.
for c,base in zip(layout['cards'],[(45,83,77),(49,46,41),(43,46,47)]):
 x,y,w,h = (c[k] for k in ('x','y','w','h'))
 panel=Image.new('RGB',(w*S,h*S)); p=panel.load()
 for yy in range(h*S):
  for xx in range(w*S):
   light=math.exp(-((xx/S-w*.8)/(w*.8))**2-((yy/S)/(h*1.1))**2)
   p[xx,yy]=tuple(round(v+light*10) for v in base)
 mask=Image.new('L',panel.size);ImageDraw.Draw(mask).rounded_rectangle((0,0,w*S-1,h*S-1),12*S,fill=255)
 im.paste(panel,(x*S,y*S),mask)
 # Short top reflection; no heavy card outlines.
 line((x+13,y+.7,x+w-13,y+.7),tuple(v+25 for v in base),.5)
# Music: fine grooves and a cream centre label.
ellipse((221,53,290,122),(23,43,40))
ellipse((219,50,287,118),(22,33,32),(94,126,113),.7)
for r in range(13,33,3): ellipse((253-r,84-r,253+r,84+r),None,(48,68,60),.5)
for a in (-60,120):
 d.arc(box((223,54,283,114)),a,a+45,fill=(119,146,126),width=2)
ellipse((241,72,265,96),(230,211,171))
ellipse((244,75,262,93),None,(184,163,123),.6)
ellipse((251,82,255,86),(38,58,50))
# Radio: a tuning window and speaker grille, clear even at native size.
rr((26,139,63,163),5,(34,32,29),(174,155,121),.8)
rr((30,143,49,147),1,(183,160,112))
line((43,142,43,148),(241,221,177),1)
for yy in (152,155,158):line((30,yy,48,yy),(128,117,94),.65)
ellipse((53,144,59,150),(215,190,142));ellipse((54,155,58,159),(144,131,103))
# Companion: the original tiny face; no extra frame inside the card.
ellipse((180,145,186,151),(238,232,219));ellipse((201,145,207,151),(238,232,219))
rr((188,157,201,159),.8,(238,232,219))
# Keycaps share exact placement with both rendering paths.
for c in layout['cards']:
 x,y=c['keyX'],c['keyY'];rr((x,y,x+18,y+18),5,None,(127,143,132) if c['key']=='M' else (110,109,99),.6)
im=im.resize((320,240),Image.Resampling.LANCZOS)
asset=root/'assets/home-stage.jpg';im.save(asset,quality=95,subsampling=0)
encoded=base64.b64encode(asset.read_bytes()).decode()
css='''/* BEGIN HOME STAGE */
.screen[data-app="home"]{width:340px;height:260px}
.launcher-view{position:relative;min-height:240px;height:240px;padding:0;background:url("data:image/jpeg;base64,'''+encoded+'''") 0 0/320px 240px}
.launcher-title{position:absolute;left:16px;top:12px;font-size:16px;line-height:23px;font-weight:700;letter-spacing:.04em;color:#f6f1e5}
.launcher-view .app-card{position:absolute;padding:0;margin:0;border:0;background:transparent;box-shadow:none;border-radius:12px;display:block;text-align:left}
.launcher-view .app-card strong{position:absolute;font-size:16px;line-height:23px;font-weight:700;color:#f6f1e5;white-space:nowrap}
.launcher-view .app-card small{position:absolute;font-size:10px;line-height:13px;letter-spacing:.02em;margin:0;opacity:1;white-space:nowrap}
.launcher-view .app-card .keycap{position:absolute;width:18px;height:18px;line-height:18px;text-align:center;font-size:12px;color:#f6f1e5}
.launcher-view .app-card:hover{background:#ffffff06;filter:none}
.launcher-view .app-card:focus-visible{outline:2px solid #ead5a8;outline-offset:2px}
.launcher-view .launcher-hint{position:absolute;left:0;top:220px;width:320px;margin:0;text-align:center;font-size:12px;line-height:18px;color:#a6ada3}
'''
html=[];rows=[]
for c in layout['cards']:
 sel='.launcher-view .'+c['style']
 css+=f"{sel}{{left:{c['x']}px;top:{c['y']}px;width:{c['w']}px;height:{c['h']}px}}\n"
 for elem,prefix in [('strong','title'),('small','subtitle'),('.keycap','key')]:
  color=f"color:{c['subtitleColor']};" if prefix=='subtitle' else ''
  css+=f"{sel} {elem}{{left:{c[prefix+'X']-c['x']}px;top:{c[prefix+'Y']-c['y']}px;{color}}}\n"
 html.append(f'<button class="app-card {c["style"]}" id="{c["id"]}"><strong>{c["title"]}</strong><small>{c["subtitle"]}</small><span class="keycap">{c["key"]}</span></button>')
 values=[str(c[k]) for k in ('x','y','w','h','titleX','titleY','subtitleX','subtitleY','keyX','keyY')]
 values += [json.dumps(c[k],ensure_ascii=False) for k in ('title','subtitle','key')]
 values += ['0x'+c['subtitleColor'][1:]]
 rows.append('    {'+','.join(values)+'},')
css+='/* END HOME STAGE */'
p=root/'music_bridge/preview.html';s=p.read_text()
a=s.index('/* BEGIN HOME STAGE */');b=s.index('/* END HOME STAGE */',a)+len('/* END HOME STAGE */');s=s[:a]+css+s[b:]
a=s.index('<div class="launcher-view">');b=s.index('<div class="radio-view">',a)
power=re.search(r'<div[^>]+id="homePower".*?</div>',s[a:b],re.S)
assert power,'Do not drop the live battery component'
s=s[:a]+'<div class="launcher-view"><div class="launcher-title">FACES</div>'+power.group(0)+''.join(html)+'<div class="launcher-hint">'+layout['hint']+'</div></div>'+s[b:]
s=s.replace('right:16px;top:12px;display:flex','right:16px;top:17px;display:flex')
p.write_text(s)
(root/'src/HomeLayout.h').write_text('''// Generated by tools/build_home_art.py from ui/home-layout.json.
#pragma once
#include <cstdint>
namespace home_layout {
struct Card { int x,y,w,h,titleX,titleY,subtitleX,subtitleY,keyX,keyY; const char *title,*subtitle,*key; uint32_t subtitleColor; };
inline constexpr Card cards[] = {
'''+ '\n'.join(rows)+'\n};\ninline constexpr const char* hint='+json.dumps(layout['hint'],ensure_ascii=False)+';\n}\n')
print(asset,len(asset.read_bytes()))
