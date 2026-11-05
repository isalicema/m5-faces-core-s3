#include "../src/MusicShelfCache.h"
#include <cassert>
#include <string>
struct Entry{std::string id;uint32_t used=0;};
int main(){
    using namespace music_shelf;
    Entry entries[11];std::string wanted[9];
    for(int i=0;i<9;++i)wanted[i]=std::to_string(i);
    for(int i=0;i<9;++i){auto* e=cacheSlot(entries,wanted[i],wanted);assert(e);e->id=wanted[i];e->used=i+1;}
    entries[9]={"old-a",1};entries[10]={"old-b",2};
    assert(cacheSlot(entries,"0",wanted)==&entries[0]);
    assert(cacheSlot(entries,"new",wanted)==&entries[9]);
    entries[9]={"new",20};assert(cacheSlot(entries,"next",wanted)==&entries[10]);
    // Neighbours survive directional reversal; prefetched order always starts with the centre.
    for(int d:{-1,1}){assert(prefetchOffset(0,d)==0);for(int i=1;i<5;++i)assert(std::abs(prefetchOffset(i,d))<=2);
        assert(prefetchOffset(5,d)==3*d&&prefetchOffset(6,d)==4*d);}
    Entry full[9];for(int i=0;i<9;++i)full[i]={wanted[i],unsigned(i)};
    assert(cacheSlot(full,"outside",wanted)==nullptr); // never evict a protected active image
    wanted[8]="outside";assert(cacheSlot(full,"outside",wanted)==&full[8]);
}
