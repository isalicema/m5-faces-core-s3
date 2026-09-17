#include "../src/RadioLayout.h"
#include "../src/RadioModel.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace radio_layout;
    static_assert(radioStationCount==3);
    // Every native-screen pixel belongs to at most one tap target.
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
        int n=home.contains(x,y)+play.contains(x,y)+connection.contains(x,y)+volumeDown.contains(x,y)+volumeUp.contains(x,y);
        for(auto r:stations)n+=r.contains(x,y);
        assert(n<=1);
    }
    for(int i=0;i<3;++i){
        const auto r=stations[i];
        assert(r.w>=44&&r.h>=40);
        assert(stationAt(r.x,r.y)==i);
        assert(stationAt(r.x+r.w-1,r.y+r.h-1)==i);
        assert(stationAt(r.x+r.w,r.y)==-1);
    }
    assert(stationAt(109,194)==-1&&stationAt(212,194)==-1);
    assert(home.contains(120,20)&&!home.contains(200,20));
    std::cout<<"PASS: native 320x240 radio hit regions, card edges, gaps and return/status separation\n";
}
