#! /bin/sh
rm -rf $PWD/install
meson install -C build --destdir $PWD/install --no-rebuild --tags=runtime $* || exit 1
