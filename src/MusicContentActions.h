#pragma once
#include "MusicLayout.h"
namespace music_content {
constexpr int width=music_layout::collectionEntryWidth,height=music_layout::collectionEntryHeight,gap=5,likeX=music_layout::metaX,collectionX=likeX+width+gap;
enum Hit {None,Like,Collection};
inline Hit touch(int x,int y,int top){
    // Four pixels above/below, clipped before the lyric row. Sibling targets never overlap.
    if(y<top-4||y>=top+height+4||y>=145)return None;
    if(x>=likeX-2&&x<likeX+width+2)return Like;
    if(x>=collectionX-2&&x<collectionX+width+2)return Collection;
    return None;
}
}
