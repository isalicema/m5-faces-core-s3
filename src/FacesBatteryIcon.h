#pragma once
#include <M5Unified.h>
#include "FacesPowerModel.h"
namespace faces_power {
// Geometry and colours adapted from M5 StopWatch drawBatteryStatusAt.
inline int drawBattery(M5Canvas& canvas,const State& state,int right,int y,bool paper=false){
    char label[8]="--";
    if(state.valid&&state.present&&state.percent>=0)snprintf(label,sizeof(label),"%d%%",state.percent);
    uint16_t color=paper?canvas.color565(17,17,17):canvas.color565(216,223,217);
    switch(tint(state)){
        case Tint::Charging:color=paper?canvas.color565(22,132,75):canvas.color565(58,222,126);break;
        case Tint::Low:color=paper?canvas.color565(182,93,0):canvas.color565(255,190,75);break;
        case Tint::Critical:color=paper?canvas.color565(197,47,50):canvas.color565(255,91,91);break;
        default:break;
    }
    int x=right-32-canvas.textWidth(label);
    uint16_t underlay[24*13];
    if(tint(state)==Tint::Charging)
        for(int row=0;row<13;++row)for(int col=0;col<24;++col)underlay[row*24+col]=canvas.readPixel(x+col,y+row);
    canvas.drawRoundRect(x,y,24,13,3,color);
    canvas.fillRoundRect(x+24,y+4,3,6,1,color);
    if(int fill=fillWidth(state);fill>0)canvas.fillRoundRect(x+2,y+2,fill,9,2,color);
    if(tint(state)==Tint::Charging){
        // The same two wedges as StopWatch, cut out to the actual JPEG backdrop.
        for(int row=1;row<=12;++row)for(int col=8;col<=17;++col){
            bool top=row<=7&&col>=15-row&&col<=14;
            bool bottom=row>=6&&col>=11&&col<=23-row;
            if(top||bottom)canvas.drawPixel(x+col,y+row,underlay[row*24+col]);
        }
    }
    canvas.setTextColor(color);canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    canvas.drawString(label,x+32,y+7);canvas.setTextDatum(lgfx::textdatum_t::top_left);
    return x;
}
}
