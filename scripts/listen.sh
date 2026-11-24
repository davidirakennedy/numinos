#!/usr/bin/env bash
# Render a sample through a Numin and play the result. With --watch, it
# re-renders and replays every time you save a source file.
#
#   scripts/listen.sh Honey samples/vox.wav Viscosity=0.8
#   scripts/listen.sh Honey samples/vox.wav --sweep Viscosity --watch
#   scripts/listen.sh Honey samples/vox.wav --ab 2              # dry/wet every 2 s
#
# Any option numin-render accepts works here (run build/dev/tools/numin-render).
source "$(dirname "$0")/_common.sh"

watch=0
args=()
for a in "$@"; do
    if [ "$a" = "--watch" ]; then watch=1; else args+=("$a"); fi
done
[ ${#args[@]} -ge 2 ] || { sed -n '2,10p' "$0"; exit 2; }

player=""
for p in afplay paplay aplay; do command -v $p >/dev/null && { player=$p; break; }; done

play_pid=""
render_and_play() {
    [ -n "$play_pid" ] && kill "$play_pid" 2>/dev/null || true
    local log
    if log=$(build numin-render 2>&1); then
        local out
        out=$("$TOOLS/numin-render" "${args[@]}") && echo "$out" || { echo "$out"; return; }
        local file
        file=$(echo "$out" | sed -n 's/.*-> //p')
        if [ -n "$player" ]; then $player "$file" & play_pid=$!; fi
    else
        echo "$log" | grep -E "error|warning" || echo "$log" | tail -20
        echo "!! build failed — fix and save again"
    fi
}

render_and_play
if [ $watch = 1 ]; then
    echo "Watching for changes (Ctrl-C to stop)..."
    while true; do
        wait_for_change
        printf '\n--- %s ---\n' "$(date +%H:%M:%S)"
        render_and_play
    done
else
    wait
fi
