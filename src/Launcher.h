#pragma once
#include "LauncherModel.h"
#ifdef FACES_SUITE
namespace suite { [[noreturn]] void open(App app); bool inputReady(); }
namespace cloud_app {void setup();void loop();}
namespace companion_app {void setup();void loop();}
namespace radio_app {void setup();void loop();}
#endif
