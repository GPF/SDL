#!/bin/bash
set -e

SOURCE_DIR="${PWD}/.."
BUILD_DIR="${PWD}/dcbuild"

BUILD_JOBS="$(nproc)"

ENABLE_TESTS=ON
ENABLE_EXAMPLES=ON
ENABLE_OPENGL=ON
ENABLE_GPU=OFF
ENABLE_CAMERA=OFF
ENABLE_DIALOG=OFF
ENABLE_TRAY=OFF
ENABLE_HIDAPI=OFF
ENABLE_SENSOR=OFF
ENABLE_PTHREADS=ON

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --enable-tests) ENABLE_TESTS=ON ;;
        --disable-tests) ENABLE_TESTS=OFF ;;
        --enable-examples) ENABLE_EXAMPLES=ON ;;
        --disable-examples) ENABLE_EXAMPLES=OFF ;;
        --enable-opengl) ENABLE_OPENGL=ON ;;
        --disable-opengl) ENABLE_OPENGL=OFF ;;
        --enable-gpu) ENABLE_GPU=ON ;;
        --disable-gpu) ENABLE_GPU=OFF ;;
        --enable-camera) ENABLE_CAMERA=ON ;;
        --disable-camera) ENABLE_CAMERA=OFF ;;
        --enable-dialog) ENABLE_DIALOG=ON ;;
        --disable-dialog) ENABLE_DIALOG=OFF ;;
        --enable-tray) ENABLE_TRAY=ON ;;
        --disable-tray) ENABLE_TRAY=OFF ;;
        --enable-hidapi) ENABLE_HIDAPI=ON ;;
        --disable-hidapi) ENABLE_HIDAPI=OFF ;;
        --enable-sensor) ENABLE_SENSOR=ON ;;
        --disable-sensor) ENABLE_SENSOR=OFF ;;
        --enable-pthreads) ENABLE_PTHREADS=ON ;;
        --disable-pthreads) ENABLE_PTHREADS=OFF ;;

        clean)
            cmake --build "$BUILD_DIR" --target clean
            exit 0
            ;;

        distclean)
            rm -rf "$BUILD_DIR"
            exit 0
            ;;

        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

kos-cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_INSTALL_PREFIX=/opt/toolchains/dc/kos/addons \
    -DCMAKE_INSTALL_LIBDIR=lib/dreamcast \
    -DCMAKE_INSTALL_INCLUDEDIR=include/ \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
    -DSDL_TESTS="$ENABLE_TESTS" \
    -DSDL_EXAMPLES="$ENABLE_EXAMPLES" \
    -DSDL_OPENGL="$ENABLE_OPENGL" \
    -DSDL_GPU="$ENABLE_GPU" \
    -DSDL_CAMERA="$ENABLE_CAMERA" \
    -DSDL_DIALOG="$ENABLE_DIALOG" \
    -DSDL_TRAY="$ENABLE_TRAY" \
    -DSDL_HIDAPI="$ENABLE_HIDAPI" \
    -DSDL_SENSOR="$ENABLE_SENSOR" \
    -DSDL_PTHREADS="$ENABLE_PTHREADS" \
    -DSDL_RENDER_VULKAN=OFF \
    -DSDL_VULKAN=OFF \
    -DSDL_OPENVR=OFF \
    -DSDL_GPU_OPENXR=OFF \
    -DSDL_RENDER_GPU=OFF

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"