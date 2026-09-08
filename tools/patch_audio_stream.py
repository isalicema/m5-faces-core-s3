"""Apply a narrowly version-checked ESP8266Audio 2.4.1 disconnect guard.

HTTPClient.getStreamPtr() rechecks connected() and may return nullptr even
after the audio source's earlier connected() check succeeded. The observed
CoreS3 crash dereferenced this null stream in ICYStream::readInternal.
This post PlatformIO script runs after dependency resolution, before compilation.
"""
from pathlib import Path
import hashlib

BASE_HASHES = {
    "AudioFileSourceICYStream.cpp": "2c58fe6ae4617736f84f4da24db82ade550b18d56807b1edc0682c40979daddf",
    "AudioFileSourceHTTPStream.cpp": "0f29ef15e85c76f0a2bca09aeba1e903fe8f0613dfe5f6ae9f318bfb96a742db",
}
ANCHOR = "#endif\n\n    // Can't read past EOF..."
GUARD = """#endif

    // Faces: disconnect can occur between connected() and getStreamPtr().
    if (!stream) {
        cb.st(STATUS_DISCONNECTED, PSTR("Stream disconnected before read"));
        http.end();
        return 0;
    }

    // Can't read past EOF..."""


def patch(library):
    for name, expected in BASE_HASHES.items():
        path = Path(library) / "src" / name
        text = path.read_text()
        original = text.replace(GUARD, ANCHOR)
        if hashlib.sha256(original.encode()).hexdigest() != expected:
            raise RuntimeError(f"Unexpected audio source version: {path}")
        if original.count(ANCHOR) != 1:
            raise RuntimeError(f"Missing unique stream guard anchor: {path}")
        patched = original.replace(ANCHOR, GUARD)
        if patched != text:
            path.write_text(patched)
            print(f"Faces audio disconnect guard: {name}")


if "Import" in globals():
    Import("env")
    patch(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / "ESP8266Audio")
elif __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    patch(parser.parse_args().library)
