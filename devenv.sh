#! /bin/sh
meson devenv -C build --workdir . $* || exit 1
