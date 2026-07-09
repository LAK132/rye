@echo off
meson compile -C build rye || exit /b 1
meson install -C build --no-rebuild %* || exit /b 1
