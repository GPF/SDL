#!/bin/bash
set -e

SOURCE_DIR="${PWD}/.."
BUILD_DIR="${PWD}/dcbuild"
BUILD_JOBS="$(nproc)"

ENABLE_TESTS=ON
ENABLE_TEST_LIBRARY=ON
ENABLE_EXAMPLES=ON
ENABLE_OPENGL=ON
ENABLE_GPU=OFF
ENABLE_CAMERA=OFF
ENABLE_DIALOG=OFF
ENABLE_TRAY=OFF
ENABLE_HIDAPI=OFF
ENABLE_SENSOR=OFF
ENABLE_PTHREADS=ON
ENABLE_SH4ZAM=OFF

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --enable-tests)
            ENABLE_TESTS=ON
            ENABLE_TEST_LIBRARY=ON
            ;;
        --disable-tests)
            ENABLE_TESTS=OFF
            ENABLE_TEST_LIBRARY=OFF
            ;;
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
        --enable-sh4zam) ENABLE_SH4ZAM=ON ;;
        --disable-sh4zam) ENABLE_SH4ZAM=OFF ;;

        clean)
            if [ -d "$BUILD_DIR" ]; then
                cmake --build "$BUILD_DIR" --target clean
            else
                echo "Build directory does not exist: $BUILD_DIR"
            fi
            exit 0
            ;;

        install)
            cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
            cmake --install "$BUILD_DIR"
            exit 0
            ;;

        uninstall)
            if [ -f "$BUILD_DIR/install_manifest.txt" ]; then
                echo "Removing installed SDL3 Dreamcast files..."
                xargs rm -vf < "$BUILD_DIR/install_manifest.txt"
            else
                echo "No install_manifest.txt found in $BUILD_DIR"
            fi
            exit 0
            ;;

        distclean)
            if [ -f "$BUILD_DIR/install_manifest.txt" ]; then
                echo "Removing installed SDL3 Dreamcast files..."
                xargs rm -vf < "$BUILD_DIR/install_manifest.txt"
            fi

            echo "Removing build directory..."
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
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=OFF \
    -DSDL_TESTS:BOOL="$ENABLE_TESTS" \
    -DSDL_TEST_LIBRARY:BOOL=ON \
    -DSDL_EXAMPLES:BOOL="$ENABLE_EXAMPLES" \
    -DSDL_OPENGL:BOOL="$ENABLE_OPENGL" \
    -DSDL_GPU:BOOL="$ENABLE_GPU" \
    -DSDL_CAMERA:BOOL="$ENABLE_CAMERA" \
    -DSDL_DIALOG:BOOL="$ENABLE_DIALOG" \
    -DSDL_TRAY:BOOL="$ENABLE_TRAY" \
    -DSDL_HIDAPI:BOOL="$ENABLE_HIDAPI" \
    -DSDL_SENSOR:BOOL="$ENABLE_SENSOR" \
    -DSDL_PTHREADS:BOOL="$ENABLE_PTHREADS" \
    -DSDL_SH4ZAM:BOOL="$ENABLE_SH4ZAM" \
    -DSDL_RENDER_VULKAN:BOOL=OFF \
    -DSDL_VULKAN:BOOL=OFF \
    -DSDL_OPENVR:BOOL=OFF \
    -DSDL_GPU_OPENXR:BOOL=OFF \
    -DSDL_RENDER_GPU:BOOL=OFF

cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"