#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace music_shelf {
constexpr float perspective=520.f,originX=160.f,originY=110.12f,perspectiveY=103.36f;
struct Point{float x,y;};
struct Geometry {
    float offset,x,y,z,scale,angle,opacity,sine,cosine,shade,shadowY,shadowBlur,shadowAlpha;
    Point corners[4];
    Point project(float u,float v)const{
        float lx=(u-64)*scale,ly=(v-69.12f)*scale;
        float depth=z-lx*sine,factor=perspective/(perspective-depth);
        return {originX+(x+lx*cosine)*factor,perspectiveY+(originY-perspectiveY+y+ly)*factor};
    }
    // Since only Y rotates, inverse project once per column, then linearly walk its texture V.
    bool column(float screenX,float& u,float& top,float& height)const{
        float sx=screenX-originX;
        float denominator=scale*(sx*sine-perspective*cosine);
        if(std::fabs(denominator)<.001f)return false;
        float lx=(perspective*x-sx*(perspective-z))/denominator;
        float depth=perspective-z+lx*scale*sine;
        if(depth<1)return false;
        float factor=perspective/depth;
        u=lx+64;top=perspectiveY+(originY-perspectiveY+y-69.12f*scale)*factor;height=128*scale*factor;
        return true;
    }
};
inline Geometry geometry(float offset){
    Geometry g{};g.offset=offset;float a=std::fabs(offset),sign=offset<0?-1.f:offset>0?1.f:0;
    g.x=sign*(a<=1?84*a:84+44*(a-1));g.y=7*std::min(a,2.2f);
    g.scale=std::max(.64f,.94f-.13f*std::min(a,2.3f));
    g.angle=-sign*(35*std::min(a,1.f)+20*std::max(0.f,std::min(a-1,1.f)));
    g.z=-72*std::min(a,2.5f);g.opacity=std::max(0.f,1-.19f*std::min(a,1.f)-.31f*std::max(0.f,a-1));
    g.sine=std::sin(g.angle*.01745329252f);g.cosine=std::cos(g.angle*.01745329252f);
    g.shade=std::min(.72f,a*.62f);
    float prominence=std::max(0.f,1-std::min(a,1.f));
    g.shadowY=2+3*prominence;g.shadowBlur=6+6*prominence;g.shadowAlpha=.24f+.18f*prominence;
    g.corners[0]=g.project(0,0);g.corners[1]=g.project(128,0);g.corners[2]=g.project(128,128);g.corners[3]=g.project(0,128);
    return g;
}
inline bool contains(const Geometry& g,float x,float y){float u,top,height;return g.column(x,u,top,height)&&u>=0&&u<128&&y>=top&&y<top+height;}
__attribute__((always_inline)) inline uint16_t swap16(uint16_t v){return (v<<8)|(v>>8);}
__attribute__((always_inline)) inline uint16_t mix565(uint16_t foreground,uint16_t background,unsigned alpha){
    unsigned inverse=256-alpha;
    // Separate red/blue into 16-bit lanes: exact 8-bit alpha, no cross-channel carry.
    unsigned rbA=((foreground&0xf800u)<<5)|(foreground&31u),rbB=((background&0xf800u)<<5)|(background&31u);
    unsigned rb=(rbA*alpha+rbB*inverse)>>8;
    unsigned green=(((foreground>>5)&63)*alpha+((background>>5)&63)*inverse)>>8;
    return uint16_t(((rb>>5)&0xf800u)|(green<<5)|(rb&31u));
}
inline float roundedDistance(float u,float v){
    float dx=std::fabs(u-64)-61,dy=std::fabs(v-64)-61;
    float ox=std::max(0.f,dx),oy=std::max(0.f,dy);
    return (ox==0||oy==0?ox+oy:std::sqrt(ox*ox+oy*oy))+std::min(std::max(dx,dy),0.f)-3;
}
// Column projection is computed once. Row traversal keeps framebuffer and texture
// accesses local in PSRAM; no per-pixel division and no distance work in interiors.
struct RasterColumn {
    float u=0,top=0,step=0,scale=0,shadowTop=0;
    int sx=0,sx1=0,vStart=0,vStep=0,tStart=0,tStep=0;unsigned fx=0,shade=0;
    bool valid=false,cover=false;
};
template<class Pump> void rasterCard(uint16_t* frame,int width,int height,const uint16_t* texture,int textureSize,
    const Geometry& g,bool swapped,Pump pump){
    if(!frame||g.opacity<=0||width>320)return;
    // UI-only scratch storage, never on the 8KB loop task stack.
    static RasterColumn columns[320];
    float left=std::min(g.corners[0].x,g.corners[1].x),right=std::max(g.corners[0].x,g.corners[1].x);
    int xmin=std::max(0,int(std::floor(left-g.shadowBlur*.7f))),xmax=std::min(width-1,int(std::ceil(right+g.shadowBlur*.7f)));
    float radius=g.shadowBlur*.65f,inverseRadius=1/radius,textureScale=textureSize/128.f;
    unsigned opacity=unsigned(g.opacity*256),shadowAlpha=unsigned(g.shadowAlpha*g.opacity*180);
    int ymin=height,ymax=0;
    for(int x=xmin;x<=xmax;++x){
        auto& c=columns[x];float h;c.valid=g.column(x+.5f,c.u,c.top,h);if(!c.valid)continue;
        c.step=128/h;c.scale=h/128;c.shadowTop=c.top+g.shadowY;
        c.vStep=int(lroundf(c.step*65536));c.vStart=int(lroundf((.5f-c.top)*c.step*65536));
        c.tStep=int(lroundf(c.step*textureScale*65536));c.tStart=int(lroundf(((.5f-c.top)*c.step*textureScale-.5f)*65536));
        c.cover=c.u>=0&&c.u<128;
        c.shade=unsigned(g.shade*std::max(0.f,1-std::min(c.u,128-c.u)/46.08f)*61);
        float tx=std::max(0.f,std::min(float(textureSize-1),c.u*textureScale-.5f));
        c.sx=int(tx);c.sx1=std::min(textureSize-1,c.sx+1);c.fx=unsigned((tx-c.sx)*256);
        ymin=std::min(ymin,int(std::floor(c.top-radius)));ymax=std::max(ymax,int(std::ceil(c.top+h+g.shadowY+radius)));
    }
    ymin=std::max(40,ymin);ymax=std::min(std::min(height-1,174),ymax);
    for(int y=ymin;y<=ymax;++y){
        if((y&7)==0)pump();
        for(int x=xmin;x<=xmax;++x){
            const auto& c=columns[x];if(!c.valid)continue;
            int vf=c.vStart+y*c.vStep;
            float v=vf*(1.f/65536);
            bool inside=c.cover&&vf>=0&&vf<128*65536;
            unsigned alpha=0;
            if(inside){
                // Only corner/edge pixels need signed distance and square root.
                bool solid=c.u>=3&&c.u<=125&&v>=3&&v<=125;
                alpha=solid?opacity:unsigned(g.opacity*std::max(0.f,std::min(1.f,.5f-roundedDistance(c.u,v)*c.scale))*256);
            }
            auto& raw=frame[y*width+x];uint16_t bg=swapped?swap16(raw):raw;
            // Opaque card interiors hide their own shadow entirely.
            if(alpha<256){
                float sv=(y+.5f-c.shadowTop)*c.step;
                bool solidShadow=c.u>=3&&c.u<=125&&sv>=3&&sv<=125;
                unsigned a=shadowAlpha;
                if(!solidShadow){
                    float distance=std::max(0.f,roundedDistance(c.u,sv)*c.scale);
                    float fade=std::max(0.f,1-distance*inverseRadius);fade=fade*fade*(3-2*fade);
                    a=unsigned(g.shadowAlpha*g.opacity*fade*180);
                }
                if(a)bg=mix565(0,bg,a);
            }
            if(alpha){
                uint16_t color;
                if(texture){
                    int ty=std::max(0,std::min((textureSize-1)*65536,c.tStart+y*c.tStep));
                    int sy=ty>>16,sy1=std::min(textureSize-1,sy+1);unsigned fy=(ty>>8)&255;
                    auto a=texture[sy*textureSize+c.sx],b=texture[sy*textureSize+c.sx1],cc=texture[sy1*textureSize+c.sx],d=texture[sy1*textureSize+c.sx1];
                    if(swapped){a=swap16(a);b=swap16(b);cc=swap16(cc);d=swap16(d);}
                    color=mix565(mix565(d,cc,c.fx),mix565(b,a,c.fx),fy);
                }else{
                    float dx=c.u-64,dy=v-64,r2=dx*dx+dy*dy;
                    color=(r2<9||(r2>735&&r2<820))?0x7b4e:0xd656;
                }
                if(c.shade)color=mix565(0,color,c.shade);
                bg=alpha==256?color:mix565(color,bg,alpha);
            }
            raw=swapped?swap16(bg):bg;
        }
    }
}
}
