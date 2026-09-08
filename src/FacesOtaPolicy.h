#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace faces_ota {
constexpr size_t slotSize=0x640000;
constexpr const char* targetId="faces-suite-cores3-v1";
constexpr const char* marker="FACES_SUITE_OTA_V1";
inline bool validSha(const char* s){if(!s||strlen(s)!=64)return false;for(size_t i=0;i<64;++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
inline bool validSize(size_t n,size_t partition){return n>=288&&n<=slotSize&&n<=partition;}
inline bool canStart(bool idle,bool wifi,bool configured,int battery,int vbus){return idle&&wifi&&configured&&(vbus>=4000||(battery>=40&&battery<=100));}
}
