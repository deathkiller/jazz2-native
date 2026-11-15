#!/bin/bash
# Boots a Wii U bundle in Cemu under Xvfb and checks that the game reaches the main menu.
# Usage: smoke-test.sh <bundle.wuhb> <Cemu's extracted usr/bin directory> <output directory>
#
# No Jazz Jackrabbit 2 data is present, so the game stops at the main menu with its "requires original
# files" message; getting there still creates the graphics device, loads shaders and fonts and draws
# the menu. The bundle must have its conditional trap instructions patched out (patch_tw.py): Cemu 2.6
# does not evaluate a trap's condition and never moves past a `tw` or `twi` with a non-zero condition
# field, so the thread hangs on the first one, even though it never fires on hardware. Coreinit logging (settings.xml) makes Cemu write the
# game's log lines (sent through OSReport) to its log.txt, which is what is checked.
# Cemu runs in portable mode from a fresh portable/ directory: an existing one there is deleted.
set -u
WUHB=$(realpath "$1"); CEMU=$(realpath "$2"); OUT=$(realpath -m "$3")
HERE=$(cd "$(dirname "$0")" && pwd)
TIMEOUT=${TIMEOUT:-120}

mkdir -p "$OUT"
P=$CEMU/portable
rm -rf "$P"
mkdir -p "$P/sdcard/wiiu/apps/Jazz2"
cp "$HERE/settings.xml" "$P/settings.xml"

Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &
XPID=$!
export DISPLAY=:99
sleep 2
(cd "$CEMU" && exec ./Cemu -g "$WUHB") > "$OUT/cemu_stdout.txt" 2>&1 &
CPID=$!

# Wait for the menu (or for Cemu to exit), then give it a few seconds to draw before the screenshot
result=timeout
for ((i = 0; i < TIMEOUT; i++)); do
	sleep 1
	if ! kill -0 $CPID 2>/dev/null; then result=exited; break; fi
	if grep -q "Going to main menu" "$P/log.txt" 2>/dev/null; then result=menu; break; fi
done
[ "$result" = menu ] && sleep 10
import -window root "$OUT/screenshot.png" 2>/dev/null
kill $CPID 2>/dev/null; sleep 2; kill -9 $CPID 2>/dev/null; kill $XPID 2>/dev/null
cp "$P/log.txt" "$OUT/cemu_log.txt" 2>/dev/null
grep -a 'coreinit.OSReport' "$OUT/cemu_log.txt" 2>/dev/null > "$OUT/game_log.txt"
echo "Game log: $(wc -l < "$OUT/game_log.txt") lines after ${i}s ($result)"

fail=0
check() { if grep -q -- "$1" "$OUT/game_log.txt"; then echo "ok: $2"; else echo "FAILED: $2"; fail=1; fi; }
check "GX2 device initialized" "graphics device created"
check "Going to main menu" "main menu reached"
if grep -q '"\[F\]' "$OUT/game_log.txt"; then echo "FAILED: fatal log lines:"; grep '"\[F\]' "$OUT/game_log.txt" | head -5; fail=1; fi
# A hang shows Cemu's flat grey screen (2 colours below the menu bar); the menu has a starfield, a logo and text
colors=$(convert "$OUT/screenshot.png" -crop 1280x700+0+50 +repage -format %k info: 2>/dev/null || echo 0)
if [ "$colors" -ge 200 ]; then echo "ok: screenshot has $colors colours"; else echo "FAILED: screenshot has only $colors colours"; fail=1; fi

[ $fail = 0 ] || { echo "--- last game log lines:"; tail -15 "$OUT/game_log.txt" | cut -c1-240; }
exit $fail
