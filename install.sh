#! /bin/sh
rm -rf $PWD/install
meson install -C build --destdir $PWD/install $* || exit 1
