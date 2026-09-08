#include "../src/RadioModel.h"
#include <cassert>
#include <iostream>

int main() {
    RadioModel m;
    m.move(-1); assert(m.cursor == 2);
    assert(m.choose() && m.selected == 2);
    m.query = "AMBIENT"; m.filter();
    assert(m.matches.size() == 2);
    m.move(1); assert(m.choose() && m.selected == 1);
    m.query = "no such station"; m.filter();
    m.move(-1); assert(!m.choose() && m.selected == 1);
    m.query.clear(); m.filter(); assert(m.matches.size() == 3);
    m.selected = 0; m.tune(-1); assert(m.selected == 2);
    m.tune(1); assert(m.selected == 0);
    m.changeVolume(-1000); assert(m.volume == 0);
    m.changeVolume(1000); assert(m.volume == 160);
    std::cout << "PASS: radio search, empty results, selection, wrap, volume bounds\n";
}
