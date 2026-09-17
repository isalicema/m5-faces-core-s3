import json,threading,unittest,urllib.request,urllib.error
from music_bridge.server import make_server
from music_bridge.power import PowerTelemetry
class Backend:
 def snapshot(self):return {'ok':True,'available':False}
class PowerTests(unittest.TestCase):
 def test_state_requests_update_power_without_changing_boot_receipts(self):
  server=make_server(Backend(),'t'*40,port=0);t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
  def read(path,header=None,auth=True):
   headers={'Authorization':'Bearer '+'t'*40} if auth else {}
   if header is not None:headers['X-Faces-Power']=header
   with urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:'+str(server.server_port)+path,headers=headers),timeout=3) as r:return json.load(r)
  try:
   with self.assertRaises(urllib.error.HTTPError) as e:read('/api/power',auth=False)
   self.assertEqual(e.exception.code,401);e.exception.close()
   self.assertIsNone(read('/api/power')['device'])
   power={'valid':True,'battery_present':True,'usb_present':False,'charging':False,'percent':25,'battery_mv':3700,'vbus_mv':0}
   self.assertTrue(read('/api/state',json.dumps({'power':power}))['ok'])
   self.assertEqual(read('/api/power')['device']['power'],power)
   self.assertIsNone(read('/api/ota/status')['device'])
   for header in ['null','{',json.dumps({'power':dict(power,percent=101)}),'x'*513]:
    self.assertTrue(read('/api/state',header)['ok'])
    self.assertEqual(read('/api/power')['device']['power'],power)
  finally:server.shutdown();server.server_close();t.join(2)
if __name__=='__main__':unittest.main()
