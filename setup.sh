#! /bin/sh

case $1 in
  # wasm32)
  #   shift
  #   cross_args="--cross-file=cross/emscripten.txt --cross-file=cross/wasm32.txt"
  # ;;

  # wasm64)
  #   shift
  #   cross_args="--cross-file=cross/emscripten.txt --cross-file=cross/wasm64.txt"
  # ;;

  clang)
    export CC=clang
    export CXX=clang++
    cross_args=
  ;;

  gcc)
    export CC=gcc
    export CXX=g++
    cross_args=
  ;;

  homebrew-clang)
    export CC=$(brew --prefix llvm)/bin/clang
    export CXX=$(brew --prefix llvm)/bin/clang++
    cross_args=
  ;;

  *)
    cross_args=
  ;;
esac
case $1 in
  clang)
    shift
    export CC_FOR_BUILD=clang
    export CXX_FOR_BUILD=clang++
    meson setup --wipe $cross_args build $@ || exit 1
  ;;

  gcc)
    shift
    export CC_FOR_BUILD=gcc
    export CXX_FOR_BUILD=g++
    meson setup --wipe $cross_args build $@ || exit 1
  ;;

  homebrew-clang)
    shift
    export CC_FOR_BUILD=$(brew --prefix llvm)/bin/clang
    export CXX_FOR_BUILD=$(brew --prefix llvm)/bin/clang++
    meson setup --wipe $cross_args build $@ || exit 1
  ;;

  msvc)
    shift
    meson setup --wipe $cross_args build --vsenv $@ || exit 1
  ;;

  auto)
    shift
    meson setup --wipe $cross_args build $@ || exit 1
  ;;

  *)
    echo "./setup.sh <cross target> [native compiler] <setup args>"
    echo "examples:"
    echo "./setup.sh auto # let meson automatically find the compiler"
    echo "./setup.sh msvc"
    echo "./setup.sh msvc --buildtype release"
    echo "./setup.sh gcc --buildtype debug"
    echo "./setup.sh clang --buildtype debugoptimized"
    echo "./setup.sh wasm32 gcc # cross compile to wasm32 host-machine, use gcc for build-machine binaries"
    exit 1
  ;;
esac
