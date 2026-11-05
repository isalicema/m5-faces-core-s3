"""Build device Home assets from the approved baked source, without writing web UI.

FACES-038 / Design System 1.1.1: one card, two invisible hit regions; a semantic
circle masks the 104x92 source sample. The baked card owns the only outer border.
The source PNG was extracted byte-for-byte from the approved preview's --home-art.
Local fonts are rasterized only; font binaries are never copied.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageChops
import json, hashlib
ROOT=Path(__file__).resolve().parents[1]
S=4
layout=json.loads((ROOT/'ui/home-layout.json').read_text())
PAPER,INK=layout['paper'],layout['ink']
source_path=ROOT/'assets/home-source/home-art-approved.png'
source=Image.open(source_path).convert('RGB')
assert source.size==(320*S,240*S)
card=layout['cards'][0];left,right=card['actions'];sleeve=right['sleeve'];mask_spec=sleeve['recordSourceMask']
assert card['visualDivider'] is False
assert (left['x'],left['w'],right['x'],right['w'])==(8,184,192,120)
assert (mask_spec['shape'],mask_spec['sampleBoxWidth'],mask_spec['sampleBoxHeight'],mask_spec['radius'],mask_spec['localCenterX'],mask_spec['localCenterY'])==('circle',104,92,52,52,46)
def box(values):return tuple(round(v*S) for v in values)
def rounded_mask(x,y,w,h,r):
    im=Image.new('L',source.size);ImageDraw.Draw(im).rounded_rectangle(box((x,y,x+w,y+h)),radius=round(r*S),fill=255);return im
# Content never overwrites the card's authoritative two-pixel outline.
inside=rounded_mask(10,36,300,88,6)
record=source.crop(box((208,34,312,126)))
# The crop's hidden y34..36 rows contain the baked card border, not vinyl.
# Extend the first exposed vinyl scanline into those occluded rows. The circle
# mask stays complete; at rest these rows remain behind the authoritative frame.
record_top=record.crop((0,2*S,record.width,2*S+1)).resize((record.width,2*S),Image.Resampling.NEAREST)
record.paste(record_top,(0,0))
record_top.save(ROOT/'assets/home-record-top-repair.png',optimize=True)
circle=Image.new('L',record.size);ImageDraw.Draw(circle).ellipse(box((0,-6,104,98)),fill=255)
# Three explicit paper-only ROIs exclude the yellow arc and authoritative frame.
music_rois=[(17,44,133,84),(17,84,100,96),(16,97,55,120)]
music_mask=Image.new('L',source.size)
for roi in music_rois:
    crop=source.crop(box(roi));paper=Image.new('RGB',crop.size,PAPER)
    difference=ImageChops.difference(crop,paper)
    channels=difference.split();changed=ImageChops.lighter(ImageChops.lighter(channels[0],channels[1]),channels[2]).point(lambda value:255 if value else 0)
    # The extracted colours are source ink blended with paper, never arc yellow.
    assert not any(r>200 and g>140 and b<100 for r,g,b in crop.get_flattened_data())
    music_mask.paste(changed,box(roi)[:2])
music_foreground=source.copy()
web_foreground=Image.new('RGBA',source.size,(0,0,0,0));web_foreground.paste(source,(0,0),music_mask)
web_foreground.save(ROOT/'assets/home-music-foreground.png',optimize=True)
music_mask.save(ROOT/'assets/home-music-foreground-mask.png',optimize=True)
def text_center(d,cx,cy,text,size,font_name,tracking=0):
    f=ImageFont.truetype(font_name,round(size*S),index=0 if 'Impact' in font_name else 1)
    widths=[d.textlength(c,font=f) for c in text];total=sum(widths)+tracking*S*(len(text)-1)
    bounds=f.getbbox(text,anchor='ls');baseline=cy*S-(bounds[1]+bounds[3])/2;x=cx*S-total/2
    for c,w in zip(text,widths):d.text((round(x),round(baseline)),c,font=f,fill=INK,anchor='ls');x+=w+tracking*S

def make(pressed=None):
    result=source.copy();layer=Image.new('RGB',source.size,PAPER);draw=ImageDraw.Draw(layer)
    # Same #ffcd1c ground as the accepted Home CSS, preserving the historical art palette.
    draw.rectangle(box((192,36,310,124)),fill='#ffcd1c')
    offset=1 if pressed=='collection' else 0
    layer.paste(record,box((200,34+offset))[:2],circle)
    x,y,w,h=sleeve['x'],sleeve['y']+offset,sleeve['width'],sleeve['height']
    draw.rounded_rectangle(box((x,y,x+w,y+h)),radius=3*S,fill=INK)
    draw.rounded_rectangle(box((x+1.5,y+1.5,x+w-1.5,y+h-1.5)),radius=1.5*S,fill=PAPER)
    font='/System/Library/Fonts/Supplemental/Impact.ttf'
    # Space-between within the sleeve's 1.5px border + 6px padding.
    label_size=sleeve['labelSize'];f=ImageFont.truetype(font,round(label_size*S));label=sleeve['label'];label_w=sum(draw.textlength(c,font=f) for c in label)/S+.3*(len(label)-1)
    text_center(draw,x+7.5+label_w/2,y+h/2,label,label_size,font,.3)
    key_x=x+w-7.5-34;draw.rounded_rectangle(box((key_x,y+4,key_x+34,y+22)),radius=9*S,fill=INK)
    key=Image.new('L',(34*S,18*S));kd=ImageDraw.Draw(key);kf=ImageFont.truetype('/System/Library/Fonts/Menlo.ttc',8*S,index=1)
    b=kf.getbbox('P',anchor='ls');kd.text((round(17*S-(b[0]+b[2])/2),round(9*S-(b[1]+b[3])/2)),'P',font=kf,fill=255,anchor='ls')
    layer.paste(PAPER,box((key_x,y+4,key_x+34,y+22)),key)
    region=Image.new('L',source.size);ImageDraw.Draw(region).rectangle(box((192,34,312,126)),fill=255)
    result.paste(layer,(0,0),ImageChops.multiply(region,inside))
    if pressed=='music':
        # Only baked foreground moves. The source paper/arc/card remain stationary.
        result.paste(PAPER,(0,0,source.width,source.height),music_mask)
        result.paste(music_foreground,(0,S),music_mask)
    return result

for state,suffix in [(None,''),('music','-music-pressed'),('collection','-collection-pressed')]:
    image=make(state);image.save(ROOT/f'assets/home-stage{suffix}-preview.png',optimize=True)
    image.resize((320,240),Image.Resampling.LANCZOS).save(ROOT/f'assets/home-stage{suffix}.png',optimize=True)
# Generated touch geometry is separate from baked art: actions have no borders.
rows=[]
for c in layout['cards']:
    values=[str(c[k]) for k in ('x','y','w','h','titleX','titleY','subtitleX','subtitleY','keyX','keyY')]
    values+=[json.dumps(c[k],ensure_ascii=False) for k in ('title','subtitle','key')]
    values+=['0x'+c['subtitleColor'][1:]];rows.append('    {'+','.join(values)+'},')
header='''// Generated by tools/build_paper_home.py; device-only, never rewrites the approved web UI.
#pragma once
#include <cstdint>
namespace home_layout {
struct Card {int x,y,w,h,titleX,titleY,subtitleX,subtitleY,keyX,keyY;const char *title,*subtitle,*key;uint32_t subtitleColor;};
inline constexpr Card cards[]={
'''+ '\n'.join(rows)+'''\n};
struct Region {int x,y,w,h;};
inline constexpr Region musicNowPlaying={8,34,184,92},musicCollection={192,34,120,92};
inline constexpr uint32_t paper=0xf4e7d3;
inline constexpr int connectionBottom=34,powerRight=312,powerY=11;
inline constexpr const char* hint="IDLE 3 MIN → COMPANION";
}
'''
(ROOT/'src/HomeLayout.h').write_text(header)
manifest={'authority':'FACES-038; Design System 1.1.1','source':str(source_path.relative_to(ROOT)),'source_sha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
'sample_box':[208,34,104,92],'record_destination':[200,34],'semantic_mask':mask_spec,'sleeve':[194,98,116,26],
'border_owner':'approved baked Music card','visual_divider':False,'web_written':False}
(ROOT/'assets/home-source/device-home-manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n')
print('Generated device Home default / local M press / local P press at 1x and 4x; web untouched.')
