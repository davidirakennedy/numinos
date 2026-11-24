# Shared by the other scripts. Not meant to be run directly.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/dev"
CONFIG=RelWithDebInfo # optimized (real-time safe) but still debuggable
cd "$ROOT"

# Configure build/dev once; after that cmake re-runs itself when needed.
configure() {
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        local gen=()
        command -v ninja >/dev/null && gen=(-G Ninja)
        echo "First-time configure (downloads JUCE into .deps/ — takes a minute)..."
        cmake -S "$ROOT" -B "$BUILD" "${gen[@]}" -DCMAKE_BUILD_TYPE=$CONFIG
    fi
}

build() { # build <target>...
    configure
    local args=()
    for t in "$@"; do args+=(--target "$t"); done
    cmake --build "$BUILD" --config $CONFIG "${args[@]}"
}

TOOLS="$BUILD/tools"
ARTEFACTS="$BUILD/plugin/Numinos_artefacts/$CONFIG"

# Prints the files that changed since the last call (by mtime), using a stamp file.
STAMP="$BUILD/.watch-stamp"
changed_sources() {
    find numins plugin tools -type f \( -name '*.h' -o -name '*.cpp' -o -name 'CMakeLists.txt' \) -newer "$STAMP" 2>/dev/null
}

# Blocks until a source file changes.
wait_for_change() {
    touch "$STAMP"
    if command -v fswatch >/dev/null; then
        fswatch -1 -r -e '.*' -i '\.(h|cpp|txt)$' numins plugin tools >/dev/null
    else
        while [ -z "$(changed_sources)" ]; do sleep 0.4; done
    fi
    sleep 0.2 # let the editor finish writing
}
