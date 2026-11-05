#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>
#include "../src/MusicShelf.h"
int main(){
    using namespace music_shelf;
    assert(canRestoreFrame(true,4,4,8,8,12,12));
    assert(!canRestoreFrame(false,4,4,8,8,12,12));
    assert(!canRestoreFrame(true,3,4,8,8,12,12));
    assert(!canRestoreFrame(true,4,4,7,8,12,12));
    assert(!canRestoreFrame(true,4,4,8,8,11,12));
    struct Item {std::string id;};std::vector<Item> items={{"30"},{"20"},{"10"}};
    assert(restoredSelection(items,std::string("20"))==1);
    items={{"20"},{"10"},{"30"}};assert(restoredSelection(items,std::string("20"))==0);
    assert(restoredSelection(items,std::string("40"))==0);
    items.clear();assert(restoredSelection(items,std::string("20"))==0);
    assert(step(0,-1,59)==58);assert(step(58,1,59)==0);
    assert(step(0,-1,0)==0);assert(step(0,1,1)==0);
    assert(validId("923293254"));assert(!validId("../state"));assert(!validId(""));assert(!validId("12&x=1"));assert(!validId("123456789012345678901"));
    for(int top:{100,119}){
        assert(entryTouch(220,top,top));assert(!entryTouch(270,40,top));
        for(int y=0;y<240;++y)for(int x=0;x<320;++x){
            auto hit=music_content::touch(x,y,top);
            if(hit!=music_content::None){assert(y>=top-4&&y<top+24);assert(y<145);}
            assert(!(entryTouch(x,y,top)&&hit==music_content::Like));
        }
        assert(!entryTouch(220,top-5,top));assert(!entryTouch(220,top+24,top));
        assert(music_content::touch(205,top,top)==music_content::None);
    }
    assert(touch(195,226)==Play);assert(touch(224,100)==None);
    assert(touch(120,226)==Previous);assert(touch(300,226)==Next);
    assert(touch(45,226)==Refresh);
    assert(touch(20,20)==Back);assert(touch(240,20)==None);
    assert(touch(160,180)==None);assert(touch(-1,40)==None);
}
