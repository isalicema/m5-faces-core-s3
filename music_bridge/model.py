import hashlib
import math
import time
from datetime import datetime

SOURCES = {"com.netease.163music": "网易云音乐", "com.apple.Music": "Apple Music"}
ACTIONS = {"toggle", "previous", "next", "favorite", "volume_up", "volume_down", "repeat_one", "repeat_all"}

def number(value, default=0):
    try:
        value = float(value)
        return value if math.isfinite(value) else default
    except (TypeError, ValueError):
        return default

def normalize(raw, now=None):
    now = time.time() if now is None else now
    raw = raw if isinstance(raw, dict) else {}
    source = raw.get("bundleIdentifier")
    if source not in SOURCES or not raw.get("title"):
        return {"available": False, "source": "", "source_name": "", "track_id": "",
                "title": "在网易云或 Apple Music 播放一首歌", "artist": "", "album": "",
                "playing": False, "position": 0, "duration": 0, "favorite": None}
    text = lambda key: str(raw.get(key) or "")[:512]
    identity = [source, text("uniqueIdentifier") or text("contentItemIdentifier"),
                text("title"), text("artist"), text("album")]
    track = hashlib.sha256("\0".join(identity).encode()).hexdigest()[:24]
    duration = max(0, number(raw.get("duration")))
    playing = raw.get("playing") is True
    position = max(0, number(raw.get("elapsedTime")))
    stamp = raw.get("timestamp")
    if isinstance(stamp, str):
        try: stamp = datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp()
        except ValueError: stamp = now
    stamp = number(stamp, now)
    if playing:
        position += max(0, min(now - stamp, 86400)) * max(0, number(raw.get("playbackRate"), 1))
    if duration:
        position = min(position, duration)
    favorite = raw.get("isLiked") if raw.get("supportsIsLiked") is True else None
    if not isinstance(favorite, bool): favorite = None
    return {"available": True, "source": source, "source_name": SOURCES[source],
            "track_id": track, "title": text("title"), "artist": text("artist"),
            "album": text("album"), "playing": playing, "position": round(position, 1),
            "duration": duration, "favorite": favorite}
