#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/macOS/SmartKnob"
export CLANG_MODULE_CACHE_PATH=/tmp/smartknob-clang-cache
export SWIFTPM_MODULECACHE_OVERRIDE=/tmp/smartknob-swift-cache
swift build --build-system native -c release --disable-sandbox --cache-path /tmp/smartknob-spm-cache --scratch-path /tmp/smartknob-swift-build
APP="$ROOT/build/SmartKnob.app"
# Recreate this generated bundle so Finder metadata from an earlier launch is
# not included in the next signature. Source files are outside build/.
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp /tmp/smartknob-swift-build/release/SmartKnob "$APP/Contents/MacOS/SmartKnob"
cp "$ROOT/macOS/SmartKnob/Info.plist" "$APP/Contents/Info.plist"
# Finder can attach bundle metadata after the app is opened; it is not code.
xattr -dr com.apple.FinderInfo "$APP" 2>/dev/null || true
xattr -dr com.apple.ResourceFork "$APP" 2>/dev/null || true
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
printf 'Built %s\n' "$APP"
