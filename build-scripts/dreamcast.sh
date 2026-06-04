#!/bin/bash
# Define the path to the Dreamcast toolchain file
#export KOS_CMAKE_TOOLCHAIN="/opt/toolchains/dc/kos/utils/cmake/dreamcast.toolchain.cmake"
# Define the source directory and build directory
SOURCE_DIR="${PWD}/.."
BUILD_DIR="${PWD}/dcbuild"
# Default options
INSTALL_PREFIX="${KOS_BASE}/addons"
INSTALL_LIBDIR="lib/dreamcast"
INSTALL_INCLUDEDIR="include"
ENABLE_OPENGL=ON
ENABLE_SDL_TEST=OFF
ENABLE_SDL_TESTS=OFF
ENABLE_PTHREADS=ON
ENABLE_UNIX_TIMERS=ON
BUILD_JOBS=$(nproc) # Use all available CPU cores by default

usage() {
    cat <<EOF
Usage: $0 [options] [clean|distclean]

Install layout options:
  --prefix PATH, --install-prefix PATH
      CMake install prefix. Defaults to \${KOS_BASE}/addons.
  --libdir PATH, --install-libdir PATH
      Library install directory under prefix, or absolute path. Defaults to lib/dreamcast.
  --includedir PATH, --install-includedir PATH
      Header install directory under prefix, or absolute path. Defaults to include.

Feature options:
  --enable-opengl | --disable-opengl
  --enable-sdl-test | --disable-sdl-test
  --enable-sdl-tests | --disable-sdl-tests
  --enable-pthreads | --disable-pthreads
  --enable-unix-timers | --disable-unix-timers
EOF
}

require_value() {
    local option="$1"
    local value="$2"

    if [ -z "$value" ]; then
        echo "Missing value for $option"
        usage
        exit 1
    fi
}

# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
case $1 in
        --help|-h)
            usage
            exit 0
            ;;
        --prefix|--install-prefix)
            option="$1"
            shift
            require_value "$option" "$1"
            INSTALL_PREFIX="$1"
            ;;
        --prefix=*|--install-prefix=*)
            INSTALL_PREFIX="${1#*=}"
            ;;
        --libdir|--install-libdir)
            option="$1"
            shift
            require_value "$option" "$1"
            INSTALL_LIBDIR="$1"
            ;;
        --libdir=*|--install-libdir=*)
            INSTALL_LIBDIR="${1#*=}"
            ;;
        --includedir|--install-includedir)
            option="$1"
            shift
            require_value "$option" "$1"
            INSTALL_INCLUDEDIR="$1"
            ;;
        --includedir=*|--install-includedir=*)
            INSTALL_INCLUDEDIR="${1#*=}"
            ;;
        --enable-opengl) ENABLE_OPENGL=ON ;;
        --disable-opengl) ENABLE_OPENGL=OFF ;;
        --enable-sdl-test) ENABLE_SDL_TEST=ON ;;
        --disable-sdl-test) ENABLE_SDL_TEST=OFF ;;
        --enable-sdl-tests) ENABLE_SDL_TEST=ON; ENABLE_SDL_TESTS=ON ;;
        --disable-sdl-tests) ENABLE_SDL_TESTS=OFF ;;
        --enable-pthreads)  ENABLE_PTHREADS=ON ;;
        --disable-pthreads) ENABLE_PTHREADS=OFF ;;
        --enable-unix-timers)  ENABLE_UNIX_TIMERS=ON ;;
        --disable-unix-timers) ENABLE_UNIX_TIMERS=OFF ;;        
        clean) 
            echo "Cleaning build directory..."
            cd "$BUILD_DIR"
            make clean            
rm -rf CMakeFiles CMakeCache.txt Makefile
            exit 0
            ;;
        distclean)
            echo "Removing build directory..."
            cd "$BUILD_DIR"
            make uninstall
rm -rf "$BUILD_DIR"
exit 0
            ;;
        *) 
            echo "Unknown option: $1"
            usage
exit 1
            ;;
    esac
shift
done
# Create the build directory if it doesn't exist
mkdir -p "$BUILD_DIR"
# Navigate to the build directory
cd "$BUILD_DIR"
# Set CMake variables based on OpenGL and SDL_TESTS options
if [ "$ENABLE_OPENGL" == "ON" ]; then
CMAKE_OPTS="-DSDL_OPENGL=ON -DSDL_HAPTIC=ON"
else
CMAKE_OPTS="-DSDL_OPENGL=OFF"
fi
CMAKE_OPTS="$CMAKE_OPTS -DSDL_TEST=$ENABLE_SDL_TEST -DSDL_TESTS=$ENABLE_SDL_TESTS"
if [ "$ENABLE_PTHREADS" == "ON" ]; then
    CMAKE_OPTS="$CMAKE_OPTS -DSDL_PTHREADS=ON"
else
    CMAKE_OPTS="$CMAKE_OPTS -DSDL_PTHREADS=OFF"
fi
if [ "$ENABLE_UNIX_TIMERS" == "ON" ]; then
    CMAKE_OPTS="$CMAKE_OPTS -DSDL_TIMER_UNIX=ON"
else
    CMAKE_OPTS="$CMAKE_OPTS -DSDL_TIMER_UNIX=OFF"
fi

CMAKE_OPTS="$CMAKE_OPTS"
# Run CMake to configure the project with the selected options
cmake -DCMAKE_TOOLCHAIN_FILE="$KOS_CMAKE_TOOLCHAIN" \
      -G "Unix Makefiles" \
      $CMAKE_OPTS \
      -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
      -DCMAKE_INSTALL_LIBDIR="$INSTALL_LIBDIR" \
      -DCMAKE_INSTALL_INCLUDEDIR="$INSTALL_INCLUDEDIR" \
      "$SOURCE_DIR"
# Build the project
make -j"$BUILD_JOBS" install

# Optional: Run tests or other commands here
# Print a message indicating the build is complete
echo "Dreamcast build complete!"
