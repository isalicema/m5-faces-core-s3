#pragma once
#include <string>
#include <vector>
namespace music {
inline size_t utf8next(const std::string& s,size_t i){if(i>=s.size())return i;do{++i;}while(i<s.size()&&(static_cast<unsigned char>(s[i])&0xc0)==0x80);return i;}
inline void popChar(std::string& s){if(s.empty())return;size_t i=s.size()-1;while(i&&(static_cast<unsigned char>(s[i])&0xc0)==0x80)--i;s.resize(i);}
template<class Measure> std::vector<std::string> titleLines(std::string text,int width,Measure measure){
    std::vector<std::string> lines;
    while(!text.empty()&&lines.size()<2){
        size_t end=0;
        while(end<text.size()){size_t next=utf8next(text,end);if(measure(text.substr(0,next).c_str())>width)break;end=next;}
        if(end==text.size()){lines.push_back(text);break;}
        if(lines.size()==1){
            std::string line=text.substr(0,end);
            while(!line.empty()&&measure((line+"…").c_str())>width)popChar(line);
            lines.push_back(line+"…");break;
        }
        if(end==0)end=utf8next(text,0);
        auto space=text.rfind(' ',end);
        if(space!=std::string::npos&&space>0)end=space;
        lines.push_back(text.substr(0,end));text.erase(0,end);
        while(!text.empty()&&text.front()==' ')text.erase(0,1);
    }
    if(lines.empty())lines.emplace_back("");
    return lines;
}
}
