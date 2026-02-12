@echo off
meson devenv -C build --workdir . %* || exit /b 1
