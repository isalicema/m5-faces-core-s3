#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

struct RadioStation { const char* name; const char* genre; const char* url; unsigned color; };
inline constexpr RadioStation radioStations[] = {
    {"Groove Salad", "Ambient / downtempo", "http://ice2.somafm.com/groovesalad-128-mp3", 0x629887},
    {"Drone Zone", "Deep ambient", "http://ice2.somafm.com/dronezone-128-mp3", 0x7775A3},
    {"Secret Agent", "Cinematic / lounge", "http://ice2.somafm.com/secretagent-128-mp3", 0xB67A57},
};
inline constexpr int radioStationCount = sizeof(radioStations) / sizeof(radioStations[0]);

struct RadioModel {
    std::string query;
    std::vector<int> matches;
    int cursor = 0;
    int selected = 0;
    int volume = 48;
    bool nowPlaying = false;
    RadioModel() { filter(); }
    void filter() {
        matches.clear();
        std::string needle = query;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::tolower(c); });
        for (int i = 0; i < radioStationCount; ++i) {
            std::string hay = std::string(radioStations[i].name) + " " + radioStations[i].genre;
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return std::tolower(c); });
            if (hay.find(needle) != std::string::npos) matches.push_back(i);
        }
        cursor = 0;
    }
    void move(int delta) {
        if (!matches.empty()) cursor = (cursor + delta % int(matches.size()) + int(matches.size())) % int(matches.size());
    }
    bool choose() {
        if (matches.empty()) return false;
        selected = matches[cursor]; nowPlaying = true; return true;
    }
    void tune(int delta) { selected = (selected + delta + radioStationCount) % radioStationCount; }
    void changeVolume(int delta) { volume = std::clamp(volume + delta, 0, 160); }
};
