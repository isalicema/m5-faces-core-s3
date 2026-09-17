#include "../src/MusicControls.h"
#include "../src/MusicTextWrap.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){using namespace music;
assert(homeTouch(0,0)&&homeTouch(27,27)&&homeTouch(70,100)&&homeTouch(144,160));
assert(!homeTouch(-1,0)&&!homeTouch(0,-1)&&!homeTouch(145,100)&&!homeTouch(70,161));
assert(!homeTouch(155,95)&&!homeTouch(10,174)&&!homeTouch(10,210));
assert(key(' ')==Toggle);assert(key('j')==Previous);assert(key('J')==Previous);
assert(key('c')==RepeatOne);assert(key('C')==RepeatOne);
assert(key('v')==RepeatAll);assert(key('V')==RepeatAll);
assert(!strcmp(name(key('c')),"repeat_one"));assert(!strcmp(name(key('V')),"repeat_all"));
assert(key('k')==Next);assert(key('h')==Favorite);
for(auto k: {'u','U','d','D','s',char(27)})assert(key(k)==None);
// Exhaustively verify every screen pixel against six disjoint visible buttons.
const Action expectedActions[]={Previous,Toggle,Next,Favorite,RepeatOne,RepeatAll};
const int xs[]={7,62,121,176,223,270}, widths[]={52,56,52,44,44,43};
for(int y=0;y<240;++y)for(int x=0;x<320;++x){
    Action expected=None;
    if(y>=202&&y<232)for(int i=0;i<6;++i)if(x>=xs[i]&&x<xs[i]+widths[i])expected=expectedActions[i];
    assert(touch(x,y)==expected);
    if(expected!=None)assert(!homeTouch(x,y));
}
assert(touch(-1,210)==None&&touch(320,210)==None&&touch(100,240)==None);
assert(touch(155,95,85)==Favorite);assert(touch(155,95)==None);assert(touch(155,115,85)==None);
assert(!strcmp(name(key('h')),"favorite"));
auto measure=[](const char* v){std::string s(v);int n=0;for(size_t i=0;i<s.size();i=utf8next(s,i))++n;return n;};
assert(titleLines("Oasis",8,measure)==std::vector<std::string>{"Oasis"});
assert((titleLines("Don't Look Back in Anger",10,measure)==std::vector<std::string>{"Don't Look","Back in A…"}));
assert((titleLines("中文歌曲名字超过两行",4,measure)==std::vector<std::string>{"中文歌曲","名字超…"}));
std::cout<<"PASS controls, non-interactive gaps, UTF-8 title wrapping\n";
}
