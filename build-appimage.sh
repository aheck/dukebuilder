#!/bin/bash

set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "$0")" && pwd)
libduke_dir=$(cd -- "$repo_dir/../libduke" && pwd)
libduke_build="$libduke_dir/build"
cd "$repo_dir"

conan install . --output-folder=conan-release --build=missing -s build_type=Release

# Duke Builder links libduke and libduke-render statically.  Make sure those
# archives are compatible with this release build as well; otherwise a stale
# AddressSanitizer build of libduke produces undefined __asan_* references at
# the final application link step.
if [[ -f "$libduke_build/meson-private/coredata.dat" ]]; then
    meson configure "$libduke_build" \
        --buildtype=release -Db_sanitize=none -Drenderer=enabled
else
    native_file="$libduke_build/conan_meson_native.ini"
    [[ -f "$native_file" ]] || {
        echo "Missing $native_file; install libduke's Conan dependencies first." >&2
        exit 1
    }
    meson setup "$libduke_build" "$libduke_dir" \
        --native-file="$native_file" \
        --buildtype=release -Db_sanitize=none -Drenderer=enabled
fi
meson compile -C "$libduke_build"

meson setup build-appimage \
    --native-file=conan-release/conan_meson_native.ini \
    --buildtype=release -Db_sanitize=none --prefix=/usr

./scripts/build-appimage.sh build-appimage
