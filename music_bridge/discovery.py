"""Faces-scoped adaptation of StopWatch discovery; authenticates before token use."""
import hashlib,hmac,re,socket,threading
QUERY=b'FACES_DISCOVER_V1|'
PORT=42116

def response(query,port,token):
    if not 1<=port<=65535:raise ValueError('invalid port')
    if not query.startswith(QUERY):return None
    nonce=query[len(QUERY):]
    if not re.fullmatch(rb'[0-9a-f]{32}',nonce):return None
    body=b'FACES_BRIDGE_V1|'+nonce+b'|'+str(port).encode('ascii')
    return body+b'|'+hmac.new(token.encode(),body,hashlib.sha256).hexdigest().encode('ascii')

class DiscoveryResponder(threading.Thread):
    daemon=True
    def __init__(self,host,http_port,token,port=PORT):
        super().__init__(name='faces-discovery');self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.bind((host,port));self.sock.settimeout(.5);self.http_port=http_port;self.token=token;self.done=threading.Event()
    def run(self):
        while not self.done.is_set():
            try:
                query,peer=self.sock.recvfrom(256);data=response(query,self.http_port,self.token)
                if data:self.sock.sendto(data,peer)
            except socket.timeout:pass
            except OSError:
                if self.done.is_set():return
    def close(self):self.done.set();self.sock.close();self.join(timeout=1)
