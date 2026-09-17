#include "../src/MusicPaper.h"
#include "../src/MusicIcons.h"
#include <cassert>
#include <iostream>
int main(){
 assert(music_paper::accentInk(0x184632)==0xffffff);
 assert(music_paper::accentInk(0xedce72)==0);
 assert(music_paper::accentInk(0)==0xffffff&&music_paper::accentInk(0xffffff)==0);
 for(int y=0;y<240;++y)for(int x=0;x<320;++x){auto a=music_paper::veil(x,y);assert(a>=.349f&&a<=.601f);}
 for(int w:{43,44,52,56})for(int y=0;y<30;++y)for(int x=0;x<w;++x){
   assert(music_paper::rounded(x+.5f,y+.5f,w,30,8)==music_paper::rounded(w-x-.5f,30-y-.5f,w,30,8));
 }
 assert(sizeof(music_icons::back)==17*17&&sizeof(music_icons::play)==13*13);
 int upper=0,lower=0,left=0,right=0;
 for(int y=0;y<13;++y)for(int x=0;x<13;++x){if(y<6)upper+=music_icons::all[y*13+x];if(y>6)lower+=music_icons::all[y*13+x];if(x<6)left+=music_icons::pause[y*13+x];if(x>6)right+=music_icons::pause[y*13+x];}
 assert(upper>1000&&lower>1000&&left>1000&&right>1000);
 int partial=0;for(auto a:music_icons::heartOutline)partial+=a>0&&a<255;
 assert(partial>10);
 std::cout<<"PASS paper contrast, veil range, symmetric button silhouette and antialiased icons\n";
}
