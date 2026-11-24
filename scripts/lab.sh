#!/usr/bin/env bash
# The lab: builds the standalone Numinos app and runs it. Every time you save
# a source file it rebuilds and relaunches — and the app comes back exactly
# where you left it (same Numin, same knob positions, same looping sample).
#
#   scripts/lab.sh
#
# Drop a WAV onto the window (or "Load sample...") to loop it through the
# current Numin. "A/B: Bypass" flips between processed and dry.
# Debug output (DBG(...) or printf) appears in this terminal.
source "$(dirname "$0")/_common.sh"

if [ "$(uname)" = Darwin ]; then
    APP="$ARTEFACTS/Standalone/Numinos.app/Contents/MacOS/Numinos"
else
    APP="$ARTEFACTS/Standalone/Numinos"
fi

pid=""
stop_app() { [ -n "$pid" ] && kill "$pid" 2>/dev/null && wait "$pid" 2>/dev/null || true; pid=""; }
trap 'stop_app; exit 0' INT TERM

while true; do
    printf '\n--- building (%s) ---\n' "$(date +%H:%M:%S)"
    if build Numinos_Standalone; then
        stop_app
        "$APP" & pid=$!
        echo "Running. Save any file in numins/ or plugin/ to rebuild + relaunch. Ctrl-C to quit."
    else
        echo "!! build failed — the previous version keeps running. Fix and save again."
    fi
    wait_for_change
done
