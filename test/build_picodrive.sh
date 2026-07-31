#!/usr/bin/env bash
# Build PicoDrive's libretro core (host x86_64) for the point-to-point
# tests.  Produces ./picodrive_libretro.so next to this script.
set -e
cd "$(dirname "$0")"

PICOVER="${1:-master}"
DIR=picodrive

if [ ! -d "$DIR" ]; then
    git clone --depth 1 --recurse-submodules --shallow-submodules \
        https://github.com/irixxxx/picodrive.git "$DIR"
fi

cd "$DIR"
git submodule update --init
make -f Makefile.libretro platform=unix -j"$(nproc)"
cp picodrive_libretro.so ../picodrive_libretro.so
echo "built: $(cd ..; pwd)/picodrive_libretro.so"
