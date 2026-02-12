@echo off
rmdir /s /q %CD%\install
meson install -C build --destdir %CD%\install %* || exit 1
