# Maple controller and Jump Pack pairing

This probe checks that SDL associates each Jump Pack (Puru) with the
controller on the same Maple port. `maple_enum_type(n, function)` indexes
devices matching a function across ports and units; it does not mean “device
at port n.” The Dreamcast joystick and haptic backends share a port-based
lookup so controller instance IDs and haptic ownership use the same mapping.

Build the probe against the SDL2 archive being tested:

```sh
make -B -f Makefile.dc \
  SDL2_LIBDIR=/path/to/sdl2/lib/dreamcast \
  SDL2_INCLUDEDIR=/path/to/sdl2/include/SDL2 \
  SDL2_CONFIG_INCLUDEDIR=/path/to/sdl2/generated/include/SDL2
```

Run it on a Dreamcast with two controllers and a Jump Pack attached to only
one. It prints the physical Maple port/unit listing and checks joystick and
haptic counts, the haptic name, `SDL_JoystickIsHaptic()`, haptic opening, and
rumble through the owning joystick. The source computes expected ownership
independently from the SDL mapping.

## Hardware observations

The SDL rumble example was also changed to wait when startup enumeration finds
no haptic device. In one physical test, it began waiting with no Puru reported;
after the controller was unplugged and replugged, SDL reported one haptic,
opened the Jump Pack, and completed both rumble pulses. In a later run, KOS's
startup Maple listing again showed the controllers but no Puru. Moving the
Jump Pack to the other controller while the test waited did not trigger
detection during that run. The test was left waiting; it was not forcibly
terminated.

These observations show that the SDL query can use a Puru once KOS exposes it,
but do not establish why KOS sometimes omits it at startup or whether the same
sequence behaves differently in the KOS rumble example. Compare the same
physical setup with that example to isolate device discovery from SDL's
port-to-joystick association.
