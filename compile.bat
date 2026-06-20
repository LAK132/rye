@echo off
meson compile -C build %* || exit /b 1
