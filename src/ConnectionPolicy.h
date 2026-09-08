#pragma once
#include <cstdint>
#include <string>
namespace connection_policy {
constexpr uint32_t attemptMs=12000,searchMs=45000;
inline int next(int previous,bool home,bool work,int selected=-1){
    if(selected>=0)return selected==0?(home?0:-1):(work?1:-1);
    if(home&&work)return previous==0?1:0;
    return home?0:work?1:-1;
}
inline std::string passwordFor(const std::string& oldSsid,const std::string& oldPassword,const std::string& newSsid,const std::string& submitted,bool open){
    if(open)return "";
    return newSsid==oldSsid&&submitted.empty()?oldPassword:submitted;
}
inline bool validPassword(const std::string& oldSsid,const std::string& oldPassword,const std::string& newSsid,const std::string& submitted,bool open){
    auto value=passwordFor(oldSsid,oldPassword,newSsid,submitted,open);
    if(value.empty())return open||(newSsid==oldSsid&&oldPassword.empty());
    return value.size()>=8&&value.size()<=63;
}
inline bool validPort(long n){return n>0&&n<=65535;}
}
