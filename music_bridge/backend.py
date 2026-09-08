import base64
import hashlib
import io
import json
import subprocess
import threading
import time
from collections import OrderedDict
from pathlib import Path
from PIL import Image, ImageOps
from .model import ACTIONS, normalize
from .theme import DEFAULT_THEME, stage_from_cover
from .playback import SkipTransition

ROOT = Path(__file__).resolve().parent.parent
HELPER = Path.home() / "Library/Application Support/FacesMusic/FacesMusicHelper.app/Contents/MacOS/FacesMusicHelper"

class MusicBackend:
    def __init__(self, runner=subprocess.run, helper=HELPER, media="/opt/homebrew/bin/media-control", lyrics=None):
        self.runner, self.helper, self.media = runner, str(helper), media
        self.lyrics = lyrics
        self.lock = threading.RLock()
        self.refresh_lock = threading.Lock()
        self.raw, self.updated, self.error = None, 0, "正在连接音乐播放器"
        self.metadata_missing = False
        self.capabilities, self.favorite = [], None
        self.repeat_request = None
        self.repeat_state = None
        self.repeat_generation = 0
        self.skip_transition = SkipTransition()
        self.favorite_generation = 0
        self.artworks = OrderedDict()
        self.art_track, self.art_id = "", ""
        self.theme = dict(DEFAULT_THEME)
        self.themes = OrderedDict()
        self.results = OrderedDict()
        self.stop = threading.Event()
        self.wake = threading.Event()

    def call(self, args, payload=None):
        result = self.runner(args, input=None if payload is None else json.dumps(payload),
                             capture_output=True, text=True, timeout=4, check=True)
        if len(result.stdout) > 12_000_000: raise ValueError("metadata too large")
        return json.loads(result.stdout or "null")

    def native(self, command, source, action="", expected=None):
        payload = {"command": command, "source": source, "action": action}
        if expected is not None:
            payload.update({"expected_" + k: str(expected.get(k) or "") for k in ("title", "artist", "album")})
            payload["expected_pid"] = str(expected["processIdentifier"])
        return self.call([self.helper], payload) or {}

    def refresh(self):
        # Only one producer may commit metadata/artwork, even during diagnostics.
        with self.refresh_lock:
            self._refresh()

    def _refresh(self):
        with self.lock:
            favorite_generation = self.favorite_generation
            repeat_generation = self.repeat_generation
        raw = self.call([self.media, "get"])
        if raw is None or raw == {}:
            # Missing system metadata is not evidence that playback stopped.
            # Keep the last cover/title explicitly stale; never enable actions
            # or advance the timeline using an old track in this state.
            with self.lock:
                self.skip_transition.clear()
                self.metadata_missing = True
                self.capabilities = []
                self.error = "系统暂未提供曲目信息，正在重试；音乐可能仍在播放"
            return
        state = normalize(raw)
        if self.lyrics:self.lyrics.request(state)
        # Publish transport/title first. AX status and cover rendering must not
        # hold the snapshot lock or hide a track that MediaRemote already knows.
        with self.lock:
            self.metadata_missing = False
            self.skip_transition.observe((raw.get('bundleIdentifier'), raw.get('processIdentifier')),
                                         state['available'], state['playing'], time.monotonic())
            source_changed = not self.raw or (raw.get("bundleIdentifier"), raw.get("processIdentifier")) != (self.raw.get("bundleIdentifier"), self.raw.get("processIdentifier"))
            if source_changed:
                self.repeat_request = None
                self.repeat_state = None
            track_changed = state["track_id"] != self.art_track
            self.raw, self.updated = raw, time.time()
            self.error = ""
            if source_changed or track_changed:
                self.favorite = None
                self.capabilities = ["toggle", "previous", "next"] if state["available"] else []
            if track_changed:
                self.art_track, self.art_id = state["track_id"], ""
                self.theme = dict(DEFAULT_THEME)

        native = {}
        if state["available"]:
            try: native = self.native("status", state["source"])
            except (OSError, ValueError, subprocess.SubprocessError):
                native = {"error": "音乐控制助手尚未就绪"}
        with self.lock:
            if repeat_generation == self.repeat_generation:
                mode = native.get("repeat_mode")
                self.repeat_state = ({"source": state["source"], "pid": raw.get("processIdentifier"), "mode": mode}
                                     if state["source"] == "com.apple.Music" and mode in ("off", "one", "all") else None)
            self.error = native.get("error", "")
            self.capabilities = sorted(set(["toggle", "previous", "next"] if state["available"] else []) |
                                       {a for a in native.get("actions", []) if a in ACTIONS})
            if favorite_generation == self.favorite_generation:
                self.favorite = native.get("favorite")
            # AppleScript and MediaRemote may briefly refer to different tracks
            # while skipping. Never paint the previous track's favorite on the new one.
            favorite_track = native.get("favorite_track")
            if favorite_generation == self.favorite_generation and favorite_track is not None and (not isinstance(favorite_track, dict) or
                    any(favorite_track.get(k) != state[k] for k in ("title", "artist", "album"))):
                self.favorite = None

        encoded = raw.get("artworkData") if state["available"] else None
        if encoded:
            try:
                data = base64.b64decode(encoded, validate=True)
                if len(data) > 6_000_000: raise ValueError("artwork too large")
                with Image.open(io.BytesIO(data)) as original:
                    if original.width * original.height > 16_000_000: raise ValueError("image too large")
                    cover = ImageOps.fit(original.convert("RGB"), (144, 144))
                    out = io.BytesIO(); cover.save(out, "JPEG", quality=88)
                data = out.getvalue()
                key = hashlib.sha256(data).hexdigest()[:24]
                with self.lock:
                    cached = self.themes.get(key)
                scene = None
                if cached is None:
                    theme, scene = stage_from_cover(cover)
                    theme['scene_id'] = hashlib.sha256(scene).hexdigest()[:24]
                else:
                    theme = dict(cached)
                with self.lock:
                    if scene is not None:
                        self.artworks[theme['scene_id']] = scene
                    self.themes[key] = theme
                    self.theme = dict(theme)
                    self.themes.move_to_end(key)
                    while len(self.themes) > 4: self.themes.popitem(last=False)
                    self.artworks.move_to_end(theme['scene_id'])
                    self.artworks[key] = data
                    self.artworks.move_to_end(key)
                    while len(self.artworks) > 8: self.artworks.popitem(last=False)
                    self.art_id = key
            except (ValueError, OSError, Image.DecompressionBombError):
                pass  # Keep same-track artwork, never reuse the previous song's cover.

    def snapshot(self):
        with self.lock:
            state = normalize(self.raw)
            state.update(ok=True, stale=self.metadata_missing or time.time() - self.updated > 7,
                         error=self.error, artwork_id=self.art_id, actions=list(self.capabilities), theme=dict(self.theme))
            state["connection"] = "metadata_missing" if self.metadata_missing else "ready" if state["available"] else "waiting"
            if self.metadata_missing and not state["available"]:
                state["title"] = "暂时获取不到曲目信息"
            if isinstance(self.favorite, bool): state["favorite"] = self.favorite
            if state["stale"]:
                state["actions"] = []
                state["playing"] = False
                state["position"] = normalize(self.raw, self.updated)["position"]
            elif self.skip_transition.holds_playing(time.monotonic()):
                state["playing"] = True
            state["repeat_mode"] = (self.repeat_state["mode"] if self.repeat_state and not state["stale"] and
                                    (state["source"], (self.raw or {}).get("processIdentifier")) ==
                                    (self.repeat_state["source"], self.repeat_state["pid"]) else None)
            state["repeat_requested"] = (self.repeat_request["mode"] if self.repeat_request and not state["stale"] and state["source"] == self.repeat_request["source"] else None)
            state["lyric"] = self.lyrics.snapshot(state) if self.lyrics else {"status":"disabled","lines":[]}
            return state

    def poll(self):
        while not self.stop.is_set():
            self.wake.clear()
            started = time.monotonic()
            try: self.refresh()
            except (OSError, ValueError, subprocess.SubprocessError):
                with self.lock: self.error = "暂时读不到音乐状态"
            self.wake.wait(max(.05, .75 - (time.monotonic() - started)))

    def action(self, action, track, request_id):
        if action not in ACTIONS: raise ValueError("unknown action")
        if not isinstance(request_id, str) or not 8 <= len(request_id) <= 80:
            raise ValueError("invalid request_id")
        with self.lock:
            # Keep the replay ledger bounded; uncertain requests must not toggle twice.
            now = time.monotonic()
            self.results = OrderedDict((k, v) for k, v in self.results.items() if now - v[0] < 600)
            signature = (action, track)
            if request_id in self.results:
                _, old_signature, result = self.results[request_id]
                if signature != old_signature: raise ValueError("request_id reused")
                return result
            # Apple validates the track and PID at the point of mutation. Avoid
            # a second, sometimes slow MediaRemote read before that native guard.
            fast_apple = (action == "favorite" and not self.metadata_missing and
                          time.time() - self.updated < 7 and isinstance(self.raw, dict) and
                          self.raw.get("bundleIdentifier") == "com.apple.Music" and
                          isinstance(self.raw.get("processIdentifier"), int))
            live_raw = dict(self.raw) if fast_apple else self.call([self.media, "get", "--no-artwork"])
            live = normalize(live_raw)
            if not live["available"] or live["track_id"] != track:
                return {"ok": False, "error": "歌曲已变化，请稍后再按", "code": "track_changed"}
            if action in ("repeat_one", "repeat_all"):
                self.repeat_request = None
                self.repeat_state = None
                self.repeat_generation += 1
            if action in ("toggle", "previous", "next"):
                self.skip_transition.clear()
            result = {"ok": False, "error": "指令结果未知，请先检查播放器", "code": "uncertain"}
            self.results[request_id] = (now, signature, result)
            while len(self.results) > 256: self.results.popitem(last=False)
            if action == "favorite": self.favorite_generation += 1
            try:
                if action in ("toggle", "previous", "next"):
                    command = {"toggle": "toggle-play-pause", "previous": "previous-track", "next": "next-track"}[action]
                    self.call([self.media, command])
                    result = {"ok": True, "pending": True}
                else:
                    result = (self.native("action", live["source"], action, expected=live_raw)
                              if fast_apple else self.native("action", live["source"], action))
            except (OSError, ValueError, subprocess.SubprocessError):
                pass
            if (action == "favorite" and result.get("ok") is True and result.get("pending") is not True and
                    isinstance(result.get("favorite"), bool) and
                    result.get("favorite_track") == {k: live[k] for k in ("title", "artist", "album")} and
                    normalize(self.raw)["track_id"] == live["track_id"]):
                self.favorite = result["favorite"]
            if action in ("previous", "next") and result.get("ok") is True and live["playing"]:
                self.skip_transition.arm((live_raw.get('bundleIdentifier'), live_raw.get('processIdentifier')),
                                         time.monotonic())
            if action in ("repeat_one", "repeat_all") and result.get("ok") is True:
                if live["source"] == "com.apple.Music":
                    if result.get("repeat_mode") in ("off", "one", "all"):
                        self.repeat_state = {"source":live["source"],"pid":live_raw.get("processIdentifier"),"mode":result["repeat_mode"]}
                else:
                    self.repeat_request = {"source":live["source"],"pid":live_raw.get("processIdentifier"),"mode":action.removeprefix("repeat_")}
            self.results[request_id] = (now, signature, result)
            self.wake.set()
            return result
