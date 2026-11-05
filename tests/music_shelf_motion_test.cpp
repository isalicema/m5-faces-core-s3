#include "../src/MusicShelfMotion.h"
#include "../src/MusicShelfGeometry.h"
#include "../src/MusicShelf.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
using namespace music_shelf;
void settle(Motion& m,uint32_t& time){for(int i=0;i<500&&m.moving();++i){time+=16;m.tick(time);}assert(!m.moving());assert(std::fabs(m.position-m.target)<.00001f);}
void writeFrame(const char* path,float position,bool swapped=false){
    std::vector<uint16_t> frame(320*240,0xf73a),textures[5];
    uint16_t colors[]={0x01ff,0x47e5,0xf800,0x801f,0xfbe0};
    for(int y=33;y<173;++y)for(int x=90;x<230;++x){float dx=x-160,dy=y-103;if(dx*dx+dy*dy<4900)frame[y*320+x]=mix565(0xfe83,frame[y*320+x],82);}
    for(int i=0;i<5;++i){textures[i].resize(160*160);for(int y=0;y<160;++y)for(int x=0;x<160;++x){
        auto color=colors[i];if((x/20+y/20)%2)color=mix565(0xffff,color,40);if(x<4||x>=156||y<4||y>=156)color=0xffff;
        textures[i][y*160+x]=swapped?swap16(color):color;
    }}
    if(swapped)for(auto& px:frame)px=swap16(px);
    struct Card{int i;Geometry g;};Card cards[5];for(int i=0;i<5;++i)cards[i]={i,geometry(i-2-position)};
    std::sort(cards,cards+5,[](const Card&a,const Card&b){return std::fabs(a.g.offset)>std::fabs(b.g.offset);});
    for(auto&card:cards)rasterCard(frame.data(),320,240,textures[card.i].data(),160,card.g,swapped,[]{});
    std::ofstream f(path,std::ios::binary);f<<"P6\n320 240\n255\n";
    for(auto raw:frame){auto px=swapped?swap16(raw):raw;char rgb[]={char(((px>>11)&31)*255/31),char(((px>>5)&63)*255/63),char((px&31)*255/31)};f.write(rgb,3);}
}
int main(int argc,char**argv){
    for(unsigned a=0;a<=256;++a){
        auto color=mix565(0xfe83,0xf73a,a);
        assert((color&31)==(3*a+26*(256-a))/256);
        assert(((color>>5)&63)==(52*a+57*(256-a))/256);
        assert((color>>11)==(31*a+30*(256-a))/256);
    }
    uint32_t rng=0x12345678;
    for(int i=0;i<10000;++i){rng=rng*1664525u+1013904223u;uint16_t a=rng; rng=rng*1664525u+1013904223u;uint16_t b=rng;unsigned opacity=(rng>>16)%257;
        unsigned inv=256-opacity;uint16_t expected=((((a>>11)*opacity+(b>>11)*inv)>>8)<<11)|(((((a>>5)&63)*opacity+((b>>5)&63)*inv)>>8)<<5)|(((a&31)*opacity+(b&31)*inv)>>8);
        assert(mix565(a,b,opacity)==expected);
    }
    assert(modulo(-1,59)==58&&nearest(-.6f)==-1&&nearest(-.5f)==0);
    Motion m;uint32_t now=100;m.reset(0,now);m.move(1,now);now+=80;m.tick(now);float pos=m.position,velocity=m.velocity;
    m.move(1,now);assert(m.position==pos&&m.velocity==velocity&&m.target==2);m.move(1,now);assert(m.target==3);
    m.move(-1,now);assert(m.position==pos&&m.velocity==velocity&&m.target==nearest(pos)-1);settle(m,now);
    for(int i=0;i<40;++i)m.move(1,now);assert(m.target-m.position<=4);settle(m,now);
    m.reset(0,UINT32_MAX-10);m.move(-1,UINT32_MAX-10);m.tick(20);assert(m.position<0);now=20;settle(m,now);
    // Centre tap is valid after release; dragging and interrupted motion never become a play.
    Gesture g;m.reset(0,1000);g.begin(160,100,1000,true,m);assert(m.moving());assert(g.finish(160,100,1030,m));assert(!m.moving());
    g.begin(160,100,1100,true,m);g.update(75,100,1200,m);assert(std::fabs(m.position-85.f/84)<.0001f);assert(!g.finish(75,100,1210,m));assert(m.velocity>0&&m.target==2);now=1210;settle(m,now);
    g.begin(160,100,now+10,true,m);assert(g.finish(160,100,now+30,m)); // no stale suppress flag
    m.move(1,now+40);m.tick(now+90);g.begin(160,100,now+100,true,m);assert(!g.finish(160,100,now+120,m));now+=120;settle(m,now);
    g.begin(160,100,now+10,true,m);g.update(161,125,now+20,m);assert(!g.finish(161,125,now+30,m));
    g.begin(160,100,now+40,true,m);g.update(-999,100,now+48,m);assert(std::fabs(m.position-g.startPosition)<=2.151f);assert(!g.finish(-999,100,now+49,m));assert(m.target==float(nearest(m.target)));
    TouchQueue q;TouchEvent e;q.push({TouchEvent::Down,160,100,0});for(int i=0;i<100;++i)q.push({TouchEvent::Move,int16_t(160-i),100,uint32_t(i+1)});q.push({TouchEvent::Up,60,100,102});
    assert(q.pop(e)&&e.phase==TouchEvent::Down);assert(q.pop(e)&&e.phase==TouchEvent::Move&&e.x==61);assert(q.pop(e)&&e.phase==TouchEvent::Up);assert(!q.pop(e));
    for(int i=0;i<17;++i)q.push({TouchEvent::Down,1,1,0});assert(q.pop(e)&&e.phase==TouchEvent::Cancel);assert(!q.pop(e));
    for(float offset=-2.8f;offset<2.9f;offset+=.1f){auto geo=geometry(offset);for(float u:{0.f,32.f,64.f,127.f})for(float v:{0.f,64.f,128.f}){
        auto p=geo.project(u,v);float iu,top,height;assert(geo.column(p.x,iu,top,height));assert(std::fabs(iu-u)<.001f);assert(std::fabs((p.y-top)*128/height-v)<.001f);
    }}
    for(float offset:{0.f,1.f,2.f}){auto geo=geometry(offset);float top=1000,bottom=0,left=1000,right=0;for(auto p:geo.corners){top=std::min(top,p.y);bottom=std::max(bottom,p.y);left=std::min(left,p.x);right=std::max(right,p.x);}std::cout<<"offset "<<offset<<" bbox "<<right-left<<" x "<<bottom-top<<" top "<<top<<" bottom "<<bottom<<'\n';if(offset==0)assert(std::fabs(bottom-top-120.32f)<.01f);if(offset==1)assert(std::fabs(bottom-top-95.94f)<.2f);if(offset==2)assert(std::fabs(bottom-top-72.f)<.2f);}
    std::vector<uint16_t> texture(160*160,0xf800),normal(320*240+16,0x1234),swapped(320*240+16,swap16(0x1234));
    auto geo=geometry(0);rasterCard(normal.data(),320,240,texture.data(),160,geo,false,[]{});for(auto& px:texture)px=swap16(px);rasterCard(swapped.data(),320,240,texture.data(),160,geo,true,[]{});
    for(size_t i=0;i<normal.size();++i)assert(normal[i]==swap16(swapped[i]));
    assert(normal[100*320+160]==0xf800);assert(normal[20*320+160]==0x1234);assert(normal[200*320+160]==0x1234);for(int i=320*240;i<320*240+16;++i)assert(normal[i]==0x1234);
    assert(contains(geo,160,100));assert(!contains(geo,80,100));
    assert(touch(195,226)==Play);assert(touch(120,226)==Previous);assert(touch(260,226)==Next);assert(touch(45,226)==Refresh);assert(touch(290,20)==None);assert(touch(160,100)==None);
    if(argc>1){std::string prefix=argv[1];writeFrame((prefix+"/raster-rest.ppm").c_str(),0);writeFrame((prefix+"/raster-moving.ppm").c_str(),.45f);writeFrame((prefix+"/raster-reverse.ppm").c_str(),-.55f);}
    std::cout<<"PASS spring continuation/reversal/wrap, drag release and fresh centre tap, input coalescing, projection roundtrip, byte order, clipping and card occlusion\n";
}
