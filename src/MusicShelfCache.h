#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace music_shelf {
constexpr size_t decodedCapacity=9,jpegCapacity=11,prefetchCount=9;
inline int prefetchOffset(size_t priority,int direction){
    const int forward[]={0,1,-1,2,-2,3,4,-3,-4};
    return forward[priority]*(direction<0?-1:1);
}
template<class Entries,size_t N> auto cacheSlot(Entries& entries,const std::string& id,const std::string (&wanted)[N])->decltype(&entries[0]){
    for(auto& e:entries)if(e.id==id)return &e;
    for(auto& e:entries)if(e.id.empty())return &e;
    decltype(&entries[0]) oldest=nullptr;
    for(auto& e:entries){bool protectedId=false;for(auto& w:wanted)if(e.id==w){protectedId=true;break;}
        if(!protectedId&&(!oldest||int32_t(e.used-oldest->used)<0))oldest=&e;
    }
    return oldest;
}
}
