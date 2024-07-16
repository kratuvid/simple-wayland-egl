#!/bin/sh

mkdir build &>/dev/null

set -e

exe="build/$(basename -s .cpp "$1")"
flags="$(pkg-config --cflags --libs wayland-client wayland-egl egl) -Iglad/include"

if [ ! -e "$exe" ] || [ "$1" -nt "$exe" ]; then
    echo clang++ -g -std=c++23 -pipe xdg-shell.o $flags "$1" -o "$exe" >&2
    clang++ -g -std=c++23 -pipe xdg-shell.o glad.o $flags "$1" -o "$exe"
fi

echo ./"$exe" ${@:2} >&2
./"$exe" ${@:2}
