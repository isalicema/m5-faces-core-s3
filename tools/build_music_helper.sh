#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
stage=$(mktemp -d /private/tmp/faces-music-helper.XXXXXX)
bundle="$stage/FacesMusicHelper.app"
target="${1:-$HOME/Library/Application Support/FacesMusic/FacesMusicHelper.app}"
mkdir -p "$bundle/Contents/MacOS" /private/tmp/faces-swift-cache
cat > "$bundle/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>io.github.m5-faces-core-s3.music-helper</string>
<key>CFBundleName</key><string>FacesMusicHelper</string>
<key>CFBundleExecutable</key><string>FacesMusicHelper</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>LSUIElement</key><true/>
<key>NSAppleEventsUsageDescription</key><string>让 Faces 控制 Apple Music 的播放和收藏。</string>
</dict></plist>
PLIST
swiftc -O -module-cache-path /private/tmp/faces-swift-cache mac/FacesMusic.swift -o "$bundle/Contents/MacOS/FacesMusicHelper"
xattr -d com.apple.FinderInfo "$bundle" 2>/dev/null || true
codesign --force --sign - --identifier io.github.m5-faces-core-s3.music-helper "$bundle"

codesign --verify --deep --strict "$bundle"
mkdir -p "$(dirname "$target")"
if [ -e "$target" ]; then mv "$target" "$target.previous.$(date +%Y%m%d-%H%M%S)"; fi
ditto --norsrc "$bundle" "$target"
codesign --verify --deep --strict "$target"
printf 'Installed helper: %s\n' "$target"
