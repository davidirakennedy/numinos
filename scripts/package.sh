#!/usr/bin/env bash
# Build a release installer: dist/Numinos-<version>.pkg (AU + VST3, Apple
# Silicon + Intel). macOS only.
#
# Signing is optional. Without it you get a working but unsigned .pkg (fine
# for testing; other people's Macs will warn them). To sign + notarize, set:
#   NUMINOS_APP_SIGN_ID="Developer ID Application: Numinos LLC (TEAMID)"
#   NUMINOS_INSTALLER_SIGN_ID="Developer ID Installer: Numinos LLC (TEAMID)"
#   NUMINOS_NOTARY_PROFILE=numinos   # from: xcrun notarytool store-credentials numinos
set -euo pipefail
[ "$(uname)" = Darwin ] || { echo "package.sh runs on macOS only"; exit 1; }

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION=$(sed -n 's/^project(Numinos VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
OUT="$ROOT/build/release"
ART="$OUT/plugin/Numinos_artefacts/Release"
STAGE="$OUT/pkg"
gen=(); command -v ninja >/dev/null && gen=(-G Ninja)

echo "== Building Numinos $VERSION (universal) =="
cmake -S . -B "$OUT" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DNUMINOS_INSTALL_ON_BUILD=OFF \
    -DNUMINOS_INCLUDE_SHOP=OFF # releases ship only the public Numins
cmake --build "$OUT" --config Release --target Numinos_AU Numinos_VST3 numin-check
"$OUT/tools/numin-check" >/dev/null || { echo "numin-check failed — not packaging"; exit 1; }

echo "== Signing =="
for b in "$ART/AU/Numinos.component" "$ART/VST3/Numinos.vst3"; do
    if [ -n "${NUMINOS_APP_SIGN_ID:-}" ]; then
        codesign --force --options runtime --timestamp --sign "$NUMINOS_APP_SIGN_ID" "$b"
    else
        codesign --force --sign - "$b" # ad-hoc: required to load on Apple Silicon
    fi
done

echo "== Packaging =="
rm -rf "$STAGE" && mkdir -p "$STAGE/au" "$STAGE/vst3" dist
cp -R "$ART/AU/Numinos.component" "$STAGE/au/"
cp -R "$ART/VST3/Numinos.vst3" "$STAGE/vst3/"

component_pkg() { # root, install location, id suffix
    local plist="$STAGE/$3.plist"
    pkgbuild --analyze --root "$1" "$plist" >/dev/null
    # Without this, Installer may "update" a copy found elsewhere (e.g. your
    # dev build in ~/Library) instead of installing to the right place.
    plutil -replace 0.BundleIsRelocatable -bool NO "$plist"
    pkgbuild --root "$1" --component-plist "$plist" --identifier "art.numinos.numinos.$3" \
        --version "$VERSION" --install-location "$2" "$STAGE/$3.pkg"
}
component_pkg "$STAGE/au" /Library/Audio/Plug-Ins/Components au
component_pkg "$STAGE/vst3" /Library/Audio/Plug-Ins/VST3 vst3

PKG="dist/Numinos-$VERSION.pkg"
sign=(); [ -n "${NUMINOS_INSTALLER_SIGN_ID:-}" ] && sign=(--sign "$NUMINOS_INSTALLER_SIGN_ID")
productbuild "${sign[@]+"${sign[@]}"}" --package "$STAGE/au.pkg" --package "$STAGE/vst3.pkg" "$PKG"

if [ -n "${NUMINOS_NOTARY_PROFILE:-}" ]; then
    echo "== Notarizing (a few minutes) =="
    xcrun notarytool submit "$PKG" --keychain-profile "$NUMINOS_NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo; echo "Built $PKG"
