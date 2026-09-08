#include "../src/MusicConnection.h"
#include <cassert>
int main() {
    using namespace music;
    assert(connection(false,false,true,false,0,0)==Connection::ConnectingWifi);
    assert(connection(false,true,true,false,800,0)==Connection::ConnectingBridge);
    assert(connection(false,false,true,false,10000,0)==Connection::RetryWifi);
    assert(connection(false,true,true,false,15000,0)==Connection::RetryBridge);
    assert(connection(true,true,false,true,17000,50)==Connection::Ready);
    assert(connection(true,true,true,false,17000,50)==Connection::Waiting);
    assert(connection(true,true,false,false,17000,50)==Connection::Waiting);
    assert(connection(true,false,false,true,18000,50)==Connection::Offline);
    assert(connection(true,true,false,true,18000,7001)==Connection::Offline);
    assert(connection(true,true,false,true,18000,0)==Connection::Ready);
}
