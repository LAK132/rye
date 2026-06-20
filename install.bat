@echo off
rmdir /s /q %CD%\install
meson install -C build --destdir %CD%\install --no-rebuild --tags=runtime %* || exit 1
