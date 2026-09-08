#include <cassert>
#include "ConnectionPolicy.h"
#include "LauncherModel.h"
int main(){using namespace connection_policy;
 assert(passwordFor("home","old-password","home","",false)=="old-password");
 assert(passwordFor("home","old-password","work","new-password",false)=="new-password");
 assert(!validPassword("home","old-password","work","",false));
 assert(validPassword("home","old-password","work","",true));
 assert(passwordFor("home","old-password","home","",true).empty());
 assert(validPassword("open","","open","",false));
 assert(!validPassword("home","old-password","home","short",false));
 assert(next(-1,true,true)==0);assert(next(0,true,true)==1);assert(next(1,true,true)==0);
 assert(next(0,false,true)==1);assert(next(1,true,false)==0);assert(next(1,false,false)==-1);
 assert(next(1,true,true,0)==0);assert(next(0,true,true,1)==1);assert(next(1,false,true,0)==-1);
 assert(!validPort(0)&&!validPort(65536)&&validPort(8766));
 assert(suite::key('S')==suite::App::Connection&&suite::key('s')==suite::App::Connection);
 assert(suite::touch(40,20)==suite::App::Connection);assert(suite::touch(260,20)==suite::App::Home);
 assert(suite::touch(160,20)==suite::App::Connection);
 assert(suite::touch(280,226)==suite::App::Home);
 assert(suite::touch(40,226)==suite::App::Home);
 assert(suite::touch(40,42)==suite::App::Music);
 assert(suite::touch(280,211)==suite::App::Companion);
 auto t=suite::ticket(suite::App::Connection);assert(suite::consume(t,true)==suite::App::Connection);
 assert(!suite::homeIdleDue(suite::App::Connection,999999,0));
}
