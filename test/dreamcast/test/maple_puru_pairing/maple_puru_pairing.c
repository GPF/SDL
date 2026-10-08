/*
  Focused probe: SDL Dreamcast joystick <-> Puru (Jump Pack) association.

  The association must be resolved per Maple port. maple_enum_type(n, func)
  indexes devices matching a function across all ports and units, so the n-th
  controller is not necessarily the n-th Puru. This probe builds its own
  independent port-based expectation and compares it against what SDL reports,
  so a two-controller/one-Puru layout is checked without trusting the driver's
  internal mapping.

  No PVR scene or drawing calls here: joystick and haptic polling must not own
  a scene.
*/

#include <stdio.h>

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/purupuru.h>

#include <SDL.h>

static int checks = 0;
static int failures = 0;

static void check(int cond, const char *what)
{
    ++checks;
    if (!cond) {
        ++failures;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FAIL: %s (%s)", what, SDL_GetError());
    } else {
        SDL_Log("ok: %s", what);
    }
}

/* Independent expectation: first valid device on a port with the given function */
static maple_device_t *port_device(int port, uint32_t func)
{
    for (int u = 0; u < MAPLE_UNIT_COUNT; ++u) {
        maple_device_t *dev = maple_enum_dev(port, u);
        if (dev && (dev->info.functions & func)) {
            return dev;
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int port_of_index[MAPLE_PORT_COUNT];
    int expected_count = 0;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Log("Physical Maple layout (port / unit / function):");
    for (int p = 0; p < MAPLE_PORT_COUNT; ++p) {
        for (int u = 0; u < MAPLE_UNIT_COUNT; ++u) {
            maple_device_t *dev = maple_enum_dev(p, u);
            if (dev) {
                SDL_Log("  port %d unit %d: %s", p, u, dev->info.product_name);
            }
        }
    }

    /* SDL enumerates ports in order, so device index i is the i-th port that
       has a controller. */
    for (int p = 0; p < MAPLE_PORT_COUNT; ++p) {
        if (port_device(p, MAPLE_FUNC_CONTROLLER)) {
            port_of_index[expected_count++] = p;
        }
    }

    check(SDL_NumJoysticks() == expected_count,
          "SDL joystick count matches the per-port controller count");

    int controllers_with_puru = 0;
    for (int p = 0; p < MAPLE_PORT_COUNT; ++p) {
        if (port_device(p, MAPLE_FUNC_CONTROLLER) && port_device(p, MAPLE_FUNC_PURUPURU)) {
            ++controllers_with_puru;
        }
    }

    /* Haptic list order must follow ports, not the n-th matching function. */
    check(SDL_NumHaptics() == controllers_with_puru,
          "SDL haptic count matches the number of controller ports that own a Puru");

    int haptic_index = 0;
    for (int p = 0; p < MAPLE_PORT_COUNT; ++p) {
        maple_device_t *puru = port_device(p, MAPLE_FUNC_PURUPURU);
        if (!puru || !port_device(p, MAPLE_FUNC_CONTROLLER)) {
            continue;
        }
        if (haptic_index < SDL_NumHaptics()) {
            const char *name = SDL_HapticName(haptic_index);
            check(name && SDL_strcmp(name, puru->info.product_name) == 0,
                  "haptic list entry names the Puru on its own port");
        }
        ++haptic_index;
    }

    for (int i = 0; i < expected_count; ++i) {
        SDL_Joystick *joy = SDL_JoystickOpen(i);
        if (!joy) {
            ++failures;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FAIL: could not open joystick %d (%s)",
                         i, SDL_GetError());
            continue;
        }

        int p = port_of_index[i];
        int has_puru = (port_device(p, MAPLE_FUNC_PURUPURU) != NULL);

        check(SDL_JoystickGetDeviceInstanceID(i) == i, "instance ID is the device index");
        check(SDL_JoystickIsHaptic(joy) == (has_puru ? 1 : 0),
              "haptic flag matches the Puru on this joystick's own port");

        if (has_puru) {
            SDL_Haptic *haptic = SDL_HapticOpenFromJoystick(joy);
            check(haptic != NULL, "haptic opens from the controller that owns the Puru");
            if (haptic) {
                SDL_Log("rumble on port %d via joystick %d", p, i);
                check(SDL_JoystickRumble(joy, 20000, 0, 1000) == 0, "rumble reaches the Puru on this port");
                SDL_JoystickRumble(joy, 0, 0, 0);
                SDL_HapticClose(haptic);
            }
        } else {
            /* Missing Puru: must fail cleanly, not dereference a null device. */
            check(SDL_HapticOpenFromJoystick(joy) == NULL,
                  "controller with no Puru reports no haptic");
            check(SDL_JoystickRumble(joy, 20000, 0, 1000) < 0,
                  "rumble on a controller with no Puru fails cleanly");
        }

        SDL_JoystickClose(joy);
    }

    /* Out-of-range and invalid-device paths must not crash. */
    check(SDL_HapticOpen(SDL_NumHaptics()) == NULL, "out-of-range haptic index is rejected");

    if (expected_count < 2) {
        SDL_Log("note: only one controller present, so the two-controller/one-Puru case "
                "needs hardware with two controllers attached");
    }

    SDL_Quit();

    SDL_Log("checks=%d failures=%d", checks, failures);
    if (failures == 0) {
        SDL_Log("PASS");
        return 0;
    }
    return 1;
}
