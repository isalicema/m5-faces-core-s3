"""Execute the pinned library's actual read functions with a disconnect race."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audio_patch", ROOT / "tools/patch_audio_stream.py")
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#define ESP_ARDUINO_VERSION_MAJOR 3
#define PSTR(x) x
#define sprintf_P sprintf
static unsigned clockMs=0;
unsigned millis(){return clockMs;}
void yield(){++clockMs;}
void delay(unsigned n){clockMs+=n;}
struct NetworkClient {
    std::vector<uint8_t> bytes;
    size_t pos=0;
    int available(){return int(bytes.size()-pos);}
    int read(uint8_t* dst,size_t n){n=std::min(n,bytes.size()-pos);if(n)memcpy(dst,bytes.data()+pos,n);pos+=n;return n;}
};
struct HTTP {
    NetworkClient stream;
    bool up=true, disconnectOnGet=false;
    bool connected(){return up;}
    void end(){up=false;}
    NetworkClient* getStreamPtr(){if(disconnectOnGet){up=false;return nullptr;}return &stream;}
};
struct Base {
    HTTP http;
    int pos=0,size=-1,reconnectTries=0,reconnectDelayMs=0;
    unsigned icyMetaInt=0,icyByteCount=0;
    char saveURL[128]={};
    enum {STATUS_DISCONNECTED=3,STATUS_RECONNECTING,STATUS_RECONNECTED,STATUS_NODATA};
    struct Callback {int last=0;void st(int n,const char*){last=n;}void md(const char*,bool,const char*){}} cb;
    bool open(const char*){return false;}
};
class AudioFileSourceHTTPStream: public Base {public:uint32_t readInternal(void*,uint32_t,bool);};
class AudioFileSourceICYStream: public Base {public:uint32_t readInternal(void*,uint32_t,bool);};
// FUNCTIONS
template<class Source> void check() {
    uint8_t out[16]={};
    Source normal;normal.http.stream.bytes={1,2,3,4,5,6,7,8};
    assert(normal.readInternal(out,4,true)==4);
    assert(normal.readInternal(out+4,4,false)==4);
    for(int i=0;i<8;++i)assert(out[i]==i+1);
    assert(normal.readInternal(out,4,true)==0);
    for(bool nonBlock:{false,true}) {
        Source race;race.http.disconnectOnGet=true;
        assert(race.readInternal(out,4,nonBlock)==0);
        assert(!race.http.up);
        assert(race.cb.last==Base::STATUS_DISCONNECTED);
        assert(race.readInternal(out,4,nonBlock)==0);
    }
}
int main(){
    check<AudioFileSourceHTTPStream>();check<AudioFileSourceICYStream>();
    AudioFileSourceICYStream icy;icy.icyMetaInt=4;
    icy.http.stream.bytes={1,2,3,4,0,5,6,7,8};uint8_t out[8]={};
    for(int i=0;i<4;++i)assert(icy.readInternal(out+i*2,2,true)==2);
    for(int i=0;i<8;++i)assert(out[i]==i+1);
}
'''


def function(text, name):
    start = text.index(f"uint32_t {name}::readInternal(")
    body = text.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class DisconnectTest(unittest.TestCase):
    def test_disconnect_between_checks_and_normal_audio(self):
        library = ROOT / ".pio/libdeps/suite/ESP8266Audio"
        patcher.patch(library)
        pieces = []
        for filename in patcher.BASE_HASHES:
            pieces.append(function((library / "src" / filename).read_text(), filename[:-4]))
        code = HARNESS.replace("// FUNCTIONS", "\n".join(pieces))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "test.cpp"
            executable = Path(directory) / "test"
            source.write_text(code)
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined", "-g", str(source), "-o", str(executable)], check=True, capture_output=True)
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
            subprocess.run([str(executable)], check=True, env=env, capture_output=True)
            # Prove that this same simulated race triggers the original bug.
            source.write_text(code.replace(patcher.GUARD, patcher.ANCHOR))
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined", "-g", str(source), "-o", str(executable)], check=True, capture_output=True)
            baseline = subprocess.run([str(executable)], env=env, capture_output=True)
            self.assertNotEqual(baseline.returncode, 0)
            self.assertIn(b"null pointer", baseline.stderr)


if __name__ == "__main__":
    unittest.main()
