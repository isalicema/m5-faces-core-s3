import unittest,socket,hmac,hashlib
from music_bridge.discovery import response,DiscoveryResponder
class DiscoveryTests(unittest.TestCase):
 def test_nonce_and_service_scope(self):
  for q in (b'M5DASH_DISCOVER_V1',b'FACES_DISCOVER_V1|',b'FACES_DISCOVER_V1|'+b'x'*32,b'FACES_DISCOVER_V1|'+b'a'*33):self.assertIsNone(response(q,8766,'secret'))
 def test_proof_binds_nonce_and_port_without_token(self):
  key='a-secret-token';q=b'FACES_DISCOVER_V1|'+b'a'*32;r=response(q,8766,key);self.assertNotIn(key.encode(),r)
  body,sig=r.rsplit(b'|',1);self.assertEqual(sig.decode(),hmac.new(key.encode(),body,hashlib.sha256).hexdigest())
  self.assertNotEqual(r,response(q,8767,key));self.assertNotEqual(r,response(q,8766,'different'));self.assertNotEqual(r,response(b'FACES_DISCOVER_V1|'+b'b'*32,8766,key))
 def test_udp_response(self):
  server=DiscoveryResponder('127.0.0.1',8766,'test-only',port=0);port=server.sock.getsockname()[1];server.start()
  try:
   with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
    s.settimeout(2);q=b'FACES_DISCOVER_V1|'+b'0'*32;s.sendto(q,('127.0.0.1',port));self.assertEqual(s.recv(512),response(q,8766,'test-only'))
  finally:server.close()
