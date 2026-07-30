# Native PVR fixed-class strided RGB565 test

This test exercises the guarded native Dreamcast PVR strided-texture
increment. At 640x480 the renderer's fixed class is 640 pixels:

* 600x240 static RGB565 uses a 640-pixel row pitch and POT header dimensions.
* 580x240 streaming RGB565 uses the same class and exposes its staging pitch
  through `SDL_LockTexture`.
* 300x180 RGB565 remains POT because a 640-pixel pitch would cost more than
  its 512-pixel POT row.

The single captured page shows the two strided panels beside a POT panel,
including a padded-pitch partial update. The expected result is stable row
markers, an intact yellow 64x64 update, and no checkerboard or edge corruption.

Build against the local native-PVR archive:

```sh
make -f Makefile.dc clean
make -f Makefile.dc -B SDL2_LIBDIR=/path/to/build-scripts/dcbuild
make -f Makefile.dc -B SDL2_LIBDIR=/path/to/build-scripts/dcbuild \
    HARDWARE_SCREENSHOTS=1
```

Launch the screenshot build on physical hardware and allow it to return:

```sh
kos-tool -t 192.168.0.128 -x pvr_strided_textures.elf -m .
```

Capture path:

```text
/pc/pvr_strided_textures-phase6_2.ppm
```
