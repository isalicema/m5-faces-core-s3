#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "MusicContentActions.h"
namespace music_shelf {
constexpr size_t maxItems=500;
inline bool validId(const char* id){
    if(!id||!id[0]||std::strlen(id)>20)return false;
    for(auto p=id;*p;++p)if(*p<'0'||*p>'9')return false;
    return true;
}
inline size_t step(size_t selected,int direction,size_t count){
    if(!count)return 0;
    selected%=count;
    return direction<0?(selected?selected-1:count-1):(selected+1)%count;
}
template<class Items,class Id> inline size_t restoredSelection(const Items& items,const Id& id){
    for(size_t i=0;i<items.size();++i)if(items[i].id==id)return i;
    return 0;
}
inline bool canRestoreFrame(bool ready,uint32_t savedEpoch,uint32_t epoch,uint32_t savedRevision,uint32_t revision,size_t savedSelected,size_t selected){
    return ready&&savedEpoch==epoch&&savedRevision==revision&&savedSelected==selected;
}
enum Hit {None,Back,Refresh,Previous,Next,Play};
inline bool entryTouch(int x,int y,int top){return music_content::touch(x,y,top)==music_content::Collection;}
constexpr int footerX[]={10,86,163,239},footerW[]={71,72,71,71};
inline Hit touch(int x,int y){
    if(x<0||x>=320||y<0||y>=240)return None;
    if(x>=5&&x<39&&y>=3&&y<37)return Back;
    if(y>=214&&y<239){
        constexpr Hit actions[]={Refresh,Previous,Play,Next};
        for(int i=0;i<4;++i)if(x>=footerX[i]&&x<footerX[i]+footerW[i])return actions[i];
    }
    return None;
}
}
