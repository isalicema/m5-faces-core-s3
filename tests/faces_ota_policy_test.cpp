#include "FacesOtaPolicy.h"
#include <cassert>
#include <string>
int main(){using namespace faces_ota;
 assert(validSha(std::string(64,'a').c_str()));assert(!validSha(std::string(63,'a').c_str()));assert(!validSha(std::string(64,'G').c_str()));assert(!validSha(nullptr));
 assert(validSize(slotSize,slotSize));assert(!validSize(slotSize+1,slotSize));assert(!validSize(287,slotSize));assert(!validSize(500,499));
 assert(canStart(true,true,true,40,-1));assert(canStart(true,true,true,-1,5000));
 assert(!canStart(true,true,true,101,-1));assert(!canStart(true,true,true,39,3999));assert(!canStart(false,true,true,100,5000));assert(!canStart(true,false,true,100,5000));assert(!canStart(true,true,false,100,5000));
}
