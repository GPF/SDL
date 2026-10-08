/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  Dreamcast-only Maple device mapping shared by the Dreamcast joystick and
  haptic drivers.

  A Puru (Jump Pack) device is a subunit of a controller port, so parentage has
  to be resolved per Maple port. maple_enum_type(n, func) returns the n-th
  device matching that function across all ports and units, not the device at
  "port n", so the n-th controller is not necessarily the n-th Puru. The
  joystick driver owns the port -> SDL joystick mapping and the haptic driver
  uses it, which keeps one source of truth for the association.
*/

#ifndef SDL_dreamcast_maple_h_
#define SDL_dreamcast_maple_h_

#include <dc/maple.h>

#include "../../SDL_internal.h"
#include "../SDL_sysjoystick.h"

/* First valid device on a Maple port that supports the given function code,
   or NULL. maple_enum_dev() only returns valid devices. */
static inline maple_device_t *SDL_DreamcastMaplePortDevice(int port, uint32_t func)
{
    for (int u = 0; u < MAPLE_UNIT_COUNT; ++u) {
        maple_device_t *dev = maple_enum_dev(port, u);
        if (dev && (dev->info.functions & func)) {
            return dev;
        }
    }
    return NULL;
}

/* SDL joystick instance ID for the controller on a Maple port, or -1 when that
   port has no controller in the SDL joystick list. The Dreamcast joystick
   driver reports device_index as its instance ID, so this matches what SDL core
   assigns. Safe to call from a joystick/haptic backend callback: it does not
   take the joystick lock. */
SDL_JoystickID SDL_DreamcastJoystickInstanceIDForPort(int port);

#endif /* SDL_dreamcast_maple_h_ */
