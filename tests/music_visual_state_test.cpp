#include "MusicVisualState.h"
#include <cassert>
#include <iostream>
int main(){
 music::ArtworkState a;
 assert(a.update("song A","art A"));assert(a.needsImage());a.accept();
 // Pause/resume can temporarily remove metadata, but the current picture stays.
 assert(!a.update("song A",""));assert(a.loaded=="art A");assert(!a.needsImage());
 assert(!a.update("song A","art A"));assert(!a.needsImage());
 // A different encoding of the same cover replaces the image only when ready.
 assert(!a.update("song A","art A2"));assert(a.loaded=="art A");assert(a.needsImage());
 assert(!a.update("song A","art A2"));assert(a.loaded=="art A");a.accept();assert(!a.needsImage());
 // Same album art can be reused; different song without art must not inherit it.
 assert(!a.update("song B","art A2"));assert(!a.needsImage());
 assert(a.update("song C",""));assert(a.loaded.empty());assert(!a.needsImage());
 assert(!a.update("song C","art C"));assert(a.needsImage());
 music::LyricFade f;f.select("song:10:first",1000);assert(f.opacity(1000)==0);
 assert(f.opacity(1110)==.5f);f.select("song:10:first",1130);assert(f.started==1000);
 assert(!f.active(1220));assert(f.opacity(1220)==1);
 f.select("song:14:second",1250);assert(f.opacity(1250)==0);
 f.select("song:10:first",1300);assert(f.opacity(1300)==0); // Seek backward restarts entry.
 f.select("wrap",UINT32_MAX-100);assert(f.opacity(9)==.5f);
 std::cout<<"PASS artwork retention, replacement, track isolation, lyric timing/seek/clock wrap\n";
}
