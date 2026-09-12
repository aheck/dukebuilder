#!/usr/bin/env bash
# Package an already configured Linux release build. No game data is included.
set -euo pipefail

if [[ ${1:-} == --help || $# -gt 2 ]]; then
    echo "Usage: $0 [build-directory (build-release)] [output-directory (dist)]"
    exit 0
fi
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_dir"
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    echo 'AppImage packaging currently supports Linux x86_64 only.' >&2; exit 1;
}
for tool in meson python3 curl sha256sum readelf strip; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
build_dir=$(realpath "${1:-build-release}")
# Check configuration before compiling or downloading packaging tools.
version=$(python3 - "$build_dir" <<'PY'
import json, pathlib, sys
build = pathlib.Path(sys.argv[1])
try:
    options = {x['name']: x['value'] for x in json.loads((build / 'meson-info/intro-buildoptions.json').read_text())}
    if isinstance(options.get('b_sanitize'), list):
        options['b_sanitize'] = ','.join(options['b_sanitize']) or 'none'
    for key, expected in [('buildtype', 'release'), ('b_sanitize', 'none'), ('prefix', '/usr')]:
        if options[key] != expected:
            raise ValueError(f'Configure {key}={expected} before packaging (currently {options[key]}).')
    version = json.loads((build / 'meson-info/intro-projectinfo.json').read_text())['version']
    if not version or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+-' for c in version):
        raise ValueError('Project version is not safe for an artifact filename.')
    print(version)
except (OSError, ValueError, KeyError) as e:
    sys.exit(str(e))
PY
)
meson compile -C "$build_dir"
if readelf -d "$build_dir/dukebuilder" | grep -Eq 'NEEDED.*libduke'; then
    echo 'Duke Builder must link libduke and its renderer statically.' >&2; exit 1
fi
output_dir=$(realpath -m "${2:-dist}")
mkdir -p "$output_dir/.tools"
# This release includes the AppImage output plugin. Verify even cached copies.
release=1-alpha-20251107-1
checksum=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
linuxdeploy="$output_dir/.tools/linuxdeploy-$release-x86_64.AppImage"
if [[ ! -f $linuxdeploy ]]; then
    curl --fail --location --retry 3 \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/$release/linuxdeploy-x86_64.AppImage" \
        --output "$linuxdeploy.download"
    printf '%s  %s\n' "$checksum" "$linuxdeploy.download" | sha256sum --check --status
    mv -- "$linuxdeploy.download" "$linuxdeploy"
fi
printf '%s  %s\n' "$checksum" "$linuxdeploy" | sha256sum --check --status
chmod +x "$linuxdeploy"
runtime="$output_dir/.tools/runtime-20251108-x86_64"
runtime_checksum=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
if [[ ! -f $runtime ]]; then
    curl --fail --location --retry 3 \
        https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
        --output "$runtime.download"
    printf '%s  %s\n' "$runtime_checksum" "$runtime.download" | sha256sum --check --status
    mv -- "$runtime.download" "$runtime"
fi
printf '%s  %s\n' "$runtime_checksum" "$runtime" | sha256sum --check --status
export LDAI_RUNTIME_FILE="$runtime"
staging=$(mktemp -d "$output_dir/.appimage-XXXXXXXX")
trap 'rm -rf -- "$staging"' EXIT
appdir="$staging/DukeBuilder.AppDir"
meson install -C "$build_dir" --no-rebuild --tags runtime --destdir "$appdir"
# Strip only the staged executable with the host tool. linuxdeploy bundles an
# older strip that cannot read modern RELR sections in system libraries.
strip --strip-unneeded "$appdir/usr/bin/dukebuilder"
export NO_STRIP=1
# Extraction mode avoids requiring FUSE in build containers. Static Qt's XCB
# and GLX plugins are compiled into the executable; no Qt deploy plugin needed.
cd "$staging"
export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_OUTPUT_VERSION="$version"
export LDAI_OUTPUT="$staging/DukeBuilder-$version-x86_64.AppImage"
export LDAI_NO_APPSTREAM=1
"$linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/dukebuilder" \
    --desktop-file "$appdir/usr/share/applications/dukebuilder.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/dukebuilder.svg" \
    --output appimage
mv -- "$LDAI_OUTPUT" "$output_dir/"
echo "Created $output_dir/DukeBuilder-$version-x86_64.AppImage"
