#!/bin/bash
# Define the path to the Dreamcast toolchain file
#export KOS_CMAKE_TOOLCHAIN="/opt/toolchains/dc/kos/utils/cmake/dreamcast.toolchain.cmake"
# Define the source directory and build directory
SOURCE_DIR="${PWD}/.."
BUILD_DIR="${PWD}/dcbuild"
# Default options
ENABLE_OPENGL=ON
ENABLE_SDL_TESTS=ON
ENABLE_PTHREADS=OFF
ENABLE_UNIX_TIMERS=OFF
BUILD_JOBS=$(nproc) # Use all available CPU cores by default
# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
case $1 in
        --enable-opengl) ENABLE_OPENGL=ON ;;
        --disable-opengl) ENABLE_OPENGL=OFF ;;
        --enable-sdl-tests) ENABLE_SDL_TESTS=ON ;;
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
if [ "$ENABLE_SDL_TESTS" == "ON" ]; then
CMAKE_OPTS="$CMAKE_OPTS -DSDL_TESTS=ON"
else
CMAKE_OPTS="$CMAKE_OPTS -DSDL_TESTS=OFF"
fi
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
      -DCMAKE_INSTALL_PREFIX=${KOS_BASE}/addons \
      -DCMAKE_INSTALL_LIBDIR=lib/dreamcast \
      -DCMAKE_INSTALL_INCLUDEDIR=include/dreamcast \
      "$SOURCE_DIR"
# Build the project
make -j"$BUILD_JOBS" install

# Optional: Run tests or other commands here
# Print a message indicating the build is complete
echo "Dreamcast build complete!"
