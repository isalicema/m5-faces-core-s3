#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
export PYTHONDONTWRITEBYTECODE=1
exec /usr/bin/python3 -m music_bridge.server "$@"
