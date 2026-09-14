#!/bin/bash

conan install . --output-folder=conan-release --build=missing -s build_type=Release

meson setup build-appimage \
    --native-file=conan-release/conan_meson_native.ini \
    --buildtype=release -Db_sanitize=none --prefix=/usr

./scripts/build-appimage.sh build-appimage
