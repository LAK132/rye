#! /bin/sh
meson compile -C build rye || exit 1
meson install -C build --no-rebuild $* || exit 1
