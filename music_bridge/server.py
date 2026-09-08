import argparse
import hmac
import json
import os
import secrets
import threading
import subprocess
from http.cookies import SimpleCookie, CookieError
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
from .backend import MusicBackend, ROOT
from .lyrics import LyricsService
from .discovery import DiscoveryResponder
from .ota import FirmwareCatalog


def load_token(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError:
        pass
    else:
        with os.fdopen(fd, "w") as output: json.dump({"token": secrets.token_urlsafe(32)}, output)
    token = json.loads(path.read_text())["token"]
    if not isinstance(token, str) or len(token) < 32: raise ValueError("invalid bridge token")
    return token


def make_server(backend, token, host="127.0.0.1", port=8766, ota_catalog=None):
    session = secrets.token_urlsafe(32)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args): pass  # Do not log tokens or listening history.
        def local(self): return self.client_address[0] in ("127.0.0.1", "::1")
        def authenticated(self):
            bearer = self.headers.get("Authorization", "")
            if hmac.compare_digest(bearer.encode(), ("Bearer " + token).encode()): return True
            cookies = SimpleCookie()
            try: cookies.load(self.headers.get("Cookie", ""))
            except CookieError: return False
            cookie = cookies.get("faces_session")
            return self.local() and cookie is not None and hmac.compare_digest(cookie.value, session)
        def send(self, status, value, mime="application/json", cookie=False):
            data = value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            if cookie: self.send_header("Set-Cookie", "faces_session=" + session + "; HttpOnly; SameSite=Strict; Path=/")
            self.end_headers()
            try: self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError): pass
        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/healthz": return self.send(200, {"ok": True, "service": "faces-music"})
            if path == "/" and self.local():
                return self.send(200, (ROOT / "music_bridge/preview.html").read_bytes(), "text/html; charset=utf-8")
            if path == "/client.js" and self.local():
                return self.send(200, (ROOT / "music_bridge/client.js").read_bytes(), "text/javascript; charset=utf-8")
            if not self.authenticated(): return self.send(401, {"ok": False, "error": "unauthorized"})
            if path == "/api/ota/status": return self.send(200,{"ok":True,"device":ota_catalog.status() if ota_catalog else None})
            if path == "/api/ota/manifest":
                release=ota_catalog.current() if ota_catalog else None
                return self.send(200,release.manifest()) if release else self.send(204,b'')
            if path.startswith("/api/ota/firmware/"):
                release=ota_catalog.resolve(path.rsplit('/',1)[1]) if ota_catalog else None
                if not release:return self.send(404,{'ok':False,'error':'not found'})
                self.send_response(200)
                self.send_header('Content-Type','application/octet-stream')
                self.send_header('Content-Length',str(len(release.data)))
                self.send_header('X-Firmware-SHA256',release.sha256)
                self.send_header('X-Firmware-Release',release.sha256)
                self.send_header('X-Faces-Target','faces-suite-cores3-v1')
                self.send_header('Cache-Control','no-store');self.end_headers()
                try:
                    for offset in range(0,len(release.data),65536):self.wfile.write(release.data[offset:offset+65536])
                except (BrokenPipeError,ConnectionResetError,TimeoutError):pass
                return
            if path == "/api/state": return self.send(200, backend.snapshot())
            if path == "/api/pair" and self.local():
                return self.send(200, {"token": token, "port": self.server.server_port})
            if path.startswith("/api/artwork/"):
                with backend.lock: data = backend.artworks.get(path.rsplit("/", 1)[1])
                if data: return self.send(200, data, "image/jpeg")
            self.send(404, {"ok": False, "error": "not found"})
        def do_POST(self):
            origin = self.headers.get("Origin")
            if origin and origin != "http://" + self.headers.get("Host", ""):
                return self.send(403, {"ok": False, "error": "origin rejected"})
            path = urlparse(self.path).path
            if path == "/session":
                if not self.local() or self.headers.get("Host") not in (
                    f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}") or self.headers.get("X-Faces-Local") != "1":
                    return self.send(403, {"ok": False})
                return self.send(200, {"ok": True}, cookie=True)
            if not self.authenticated(): return self.send(401, {"ok": False, "error": "unauthorized"})
            if path not in ("/api/action","/api/ota/report"): return self.send(404, {"ok": False})
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 2048: raise ValueError("invalid body size")
                self.connection.settimeout(5)
                request = json.loads(self.rfile.read(length))
                if not isinstance(request, dict): raise ValueError("invalid request")
                if path == "/api/ota/report":
                    if not ota_catalog:return self.send(404,{"ok":False})
                    ota_catalog.record_report(request);return self.send(200,{"ok":True})
                result = backend.action(request.get("action"), request.get("track_id"), request.get("request_id"))
                return self.send(200 if result.get("ok") else 409, result)
            except (ValueError, TypeError, OSError):
                return self.send(400, {"ok": False, "error": "invalid request"})
            except subprocess.SubprocessError:
                return self.send(503, {"ok": False, "error": "播放器暂时不可用"})
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    return server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lan", action="store_true", help="Enable authenticated LAN access for Faces")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--ota-directory", type=Path, default=ROOT / ".local/ota")
    parser.add_argument("--config", default=str(ROOT / ".local/music.json"))
    args = parser.parse_args()
    token = load_token(args.config)
    backend = MusicBackend(lyrics=LyricsService())
    server = make_server(backend, token, "0.0.0.0" if args.lan else "127.0.0.1", args.port, FirmwareCatalog(args.ota_directory))
    discovery = None
    if args.lan:
        try:
            discovery = DiscoveryResponder("0.0.0.0", args.port, token); discovery.start()
        except OSError:
            print("Faces discovery unavailable; configured IP remains usable", flush=True)
    worker = threading.Thread(target=backend.poll, daemon=True); worker.start()
    print(f"Faces Music preview: http://127.0.0.1:{args.port} | LAN {'on' if args.lan else 'off'}", flush=True)
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        if discovery: discovery.close()
        backend.stop.set(); backend.wake.set(); backend.lyrics.close(); server.server_close(); worker.join(timeout=5)

if __name__ == "__main__": main()
