import hashlib,json,struct,tempfile,threading,unittest,urllib.request,urllib.error
from pathlib import Path
from music_bridge.ota import FirmwareCatalog,publish,TARGET,MARKER,MAX_SIZE,validate_image
from music_bridge.server import make_server
from music_bridge.backend import MusicBackend

def fixture():
    data=bytearray(200000);data[0]=0xe9;struct.pack_into('<H',data,12,9);struct.pack_into('<I',data,32,0xabcd5432);data[300:300+len(MARKER)]=MARKER
    return bytes(data)
class OtaTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.fw=self.root/'app.bin';self.fw.write_bytes(fixture());self.directory=self.root/'ota';self.catalog=FirmwareCatalog(self.directory);self.device='02:00:00:00:00:01'
    def publish(self):return publish(self.fw,self.directory,self.device)
    def tearDown(self):self.tmp.cleanup()
    def test_publication_is_hashed_immutable_and_republishable(self):
        self.assertIsNone(self.catalog.current());m=self.publish();r=self.catalog.current()
        self.assertEqual(r.data,fixture());self.assertEqual(r.manifest()['sha256'],hashlib.sha256(r.data).hexdigest())
        self.assertEqual(r.manifest()['target'],TARGET);self.assertEqual(self.publish(),m)
        self.assertIsNone(self.catalog.resolve('0'*64))
    def test_rejects_wrong_image_family_chip_bootloader_and_size(self):
        for offset,patch in [(0,b'\0'),(12,b'\0\0'),(32,b'\0\0\0\0'),(300,b'X'*len(MARKER))]:
            data=bytearray(fixture());data[offset:offset+len(patch)]=patch
            with self.assertRaises(ValueError):validate_image(data)
        with self.assertRaises(ValueError):validate_image(b'X'*(MAX_SIZE+1))
    def test_corrupt_or_malformed_catalog_never_served(self):
        m=self.publish();self.assertIsNotNone(self.catalog.current())
        for patch in [{'size':'x'},{'size':True},{'filename':'../app.bin'},{'target':'stopwatch'},{'device_mac':'invalid'}]:
            (self.directory/'current.json').write_text(json.dumps(dict(m,**patch)));self.assertIsNone(self.catalog.current())
        (self.directory/'current.json').write_text(json.dumps(m));path=self.directory/m['filename'];data=bytearray(path.read_bytes());data[-1]=1;path.write_bytes(data)
        self.assertIsNone(self.catalog.current())
    def test_authenticated_http_manifest_binary_and_boot_receipt(self):
        m=self.publish()
        server=make_server(MusicBackend(), 't'*40, port=0,ota_catalog=self.catalog)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        base='http://127.0.0.1:'+str(server.server_port);op=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def request(path,auth=True,data=None):
            return op.open(urllib.request.Request(base+path,data=None if data is None else json.dumps(data).encode(),headers={'Authorization':'Bearer '+'t'*40} if auth else {}),timeout=3)
        try:
            for route in ['/api/ota/manifest','/api/ota/firmware/'+m['sha256'],'/api/ota/status']:
                with self.assertRaises(urllib.error.HTTPError) as err:request(route,False)
                self.assertEqual(err.exception.code,401);err.exception.close()
            with request('/api/ota/manifest') as r:self.assertEqual(json.load(r)['sha256'],m['sha256'])
            with request('/api/ota/firmware/'+m['sha256']) as r:
                self.assertEqual(r.headers['X-Faces-Target'],TARGET);self.assertEqual(hashlib.sha256(r.read()).hexdigest(),m['sha256'])
            # A disconnected download does not kill the server or mutate the release.
            r=request('/api/ota/firmware/'+m['sha256']);r.read(1);r.close()
            with request('/healthz') as r:self.assertTrue(json.load(r)['ok'])
            report={'target':TARGET,'device_mac':self.device,'elf_sha256':'a'*64,'partition':0x650000,'event':'ready'}
            with request('/api/ota/report',data=report) as r:self.assertTrue(json.load(r)['ok'])
            with request('/api/ota/status') as r:self.assertEqual(json.load(r)['device']['partition'],0x650000)
            with self.assertRaises(urllib.error.HTTPError) as err:request('/api/ota/report',data=dict(report,device_mac=123))
            self.assertEqual(err.exception.code,400);err.exception.close()
        finally:server.shutdown();server.server_close();thread.join(2)
    def test_power_report_unknown_absent_and_bad_values(self):
        self.publish();report={'target':TARGET,'device_mac':self.device,'elf_sha256':'a'*64,'partition':0x650000,'event':'ready'}
        power={'valid':True,'battery_present':True,'usb_present':True,'charging':True,'percent':72,'battery_mv':3980,'vbus_mv':5050}
        self.catalog.record_report(dict(report,power=power));self.assertEqual(self.catalog.status()['power'],power)
        for patch in ({'percent':101},{'percent':True},{'battery_mv':100},{'charging':'yes'}):
            with self.assertRaises(ValueError):self.catalog.record_report(dict(report,power=dict(power,**patch)))
        self.assertEqual(self.catalog.status()['power'],power)
        self.catalog.record_report(dict(report,power=dict(power,battery_present=False)))
        self.assertIsNone(self.catalog.status()['power']['percent']);self.assertFalse(self.catalog.status()['power']['charging'])
        self.catalog.record_report(dict(report,power=dict(power,valid=False)))
        self.assertIsNone(self.catalog.status()['power']['percent'])
        self.catalog.record_report(report);self.assertNotIn('power',self.catalog.status())
if __name__=='__main__':unittest.main()
