#!/usr/bin/env bash
# Safety checks. Always runs numin-check (NaN/inf, block-size and reset bugs).
# With --plugin, also builds the plugin and validates it the way hosts do:
#   auval      — Apple's Audio Unit validator (Logic refuses AUs that fail it)
#   pluginval  — Tracktion's validator for AU + VST3, if installed
#                (download from github.com/Tracktion/pluginval/releases)
#
#   scripts/check.sh            # fast: just the DSP
#   scripts/check.sh Honey      # just one Numin
#   scripts/check.sh --plugin   # everything
source "$(dirname "$0")/_common.sh"

plugin=0
names=()
for a in "$@"; do
    if [ "$a" = "--plugin" ]; then plugin=1; else names+=("$a"); fi
done

build numin-check
"$TOOLS/numin-check" "${names[@]+"${names[@]}"}"

[ $plugin = 1 ] || exit 0

build Numinos_All

if [ "$(uname)" = Darwin ]; then
    echo; echo "== auval =="
    killall -9 AudioComponentRegistrar 2>/dev/null || true # make macOS notice the fresh build
    auval -v aufx Nmn1 Nmns | tail -5
fi

pv=$(command -v pluginval || true)
[ -z "$pv" ] && [ -x /Applications/pluginval.app/Contents/MacOS/pluginval ] && pv=/Applications/pluginval.app/Contents/MacOS/pluginval
if [ -n "$pv" ]; then
    for p in "$ARTEFACTS"/VST3/Numinos.vst3 "$ARTEFACTS"/AU/Numinos.component; do
        [ -e "$p" ] || continue
        echo; echo "== pluginval: $(basename "$p") =="
        "$pv" --strictness-level 8 --validate-in-process --validate "$p"
    done
else
    echo; echo "(pluginval not found — skipping. It's worth installing.)"
fi
