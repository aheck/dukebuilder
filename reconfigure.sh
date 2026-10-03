#!/bin/bash

meson setup -Db_sanitize=address -Db_lundef=false --reconfigure build --native-file build/conan_meson_native.ini
