#include "../src/MusicIoPolicy.h"
#include <cassert>
int main(){
 using namespace music_io;
 assert(cancelRead(true,true));assert(!cancelRead(true,false));
 assert(!cancelRead(false,true));assert(!cancelRead(false,false)); // Never cancel/replay POST.
 ArtRetry r;
 assert(r.nextDelay(404,false,false)==250);
 assert(r.nextDelay(404,true,false)==0); // Cancellation must not consume pending backoff.
 assert(r.nextDelay(404,false,false)==500);
 assert(r.nextDelay(-1,false,false)==3000); // Real failure is never fast-polled.
 assert(r.nextDelay(404,false,false)==1000);
 for(int i=0;i<100;++i)assert(r.nextDelay(404,false,false)==3000);
 assert(r.nextDelay(200,false,true)==0);
 assert(r.nextDelay(404,false,false)==250);
}
