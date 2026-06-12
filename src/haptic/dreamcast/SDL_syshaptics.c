/*
  SDL Dreamcast Haptic (Rumble) Driver
  Based on SDL haptic dummy driver, modified for Dreamcast Jump Pack support.
*/

#include "../../SDL_internal.h"

#ifdef SDL_HAPTIC_DREAMCAST

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/purupuru.h> /* Corrected include */

#include "SDL_haptic.h"
#include "../SDL_syshaptic.h"
#include "../../joystick/SDL_sysjoystick.h"

#define MAX_HAPTIC_DEVICES 4  /* Support up to 4 controllers with jump packs */

typedef struct {
    maple_device_t *device;
    SDL_Haptic *haptic; // Pointer to the SDL_Haptic device
} haptic_hwdata;

/*
 * Haptic system effect data for Dreamcast.
 */
struct haptic_hweffect {
    SDL_HapticEffect effect;  // Stores the SDL haptic effect data
    int is_running;           // Indicates whether the effect is currently running
    uint8_t intensity;        // Stores the intensity for rumble (0-255)
    uint16_t length;          // Duration for the rumble effect in milliseconds
    purupuru_effect_t rumble; // Cached Dreamcast rumble packet
};

static uint8_t DREAMCAST_ClampRumbleLevel(Uint16 magnitude)
{
    uint32_t level = ((uint32_t)magnitude * 7u + 16383u) / 32767u;
    if (level > 7u) {
        level = 7u;
    }
    if (level == 0u && magnitude > 0u) {
        level = 1u;
    }
    return (uint8_t)level;
}

static void DREAMCAST_BuildRumblePacket(purupuru_effect_t *out, Uint16 magnitude, Uint32 length)
{
    uint8_t level;

    SDL_memset(out, 0, sizeof(*out));
    out->motor = 1;

    if (magnitude == 0) {
        return;
    }

    level = DREAMCAST_ClampRumbleLevel(magnitude);
    out->fpow = level;
    out->freq = 26;

    /* The PuruPuru packet has no true millisecond duration field, so map the
       SDL length onto the 8-bit inclination byte as a best-effort proxy. */
    out->inc = (uint8_t)SDL_min(length / 10u, 255u);
    if (out->inc == 0) {
        out->inc = 1;
    }
}

static haptic_hwdata haptic_devices[MAX_HAPTIC_DEVICES] = { {NULL} };

static int DREAMCAST_InitHapticHandle(SDL_Haptic *haptic, int index)
{
    haptic->index = index;
    haptic->supported = SDL_HAPTIC_LEFTRIGHT;
    haptic->neffects = 1;
    haptic->nplaying = 1;

    haptic->effects = (struct haptic_effect *)SDL_malloc(sizeof(struct haptic_effect) * haptic->neffects);
    if (!haptic->effects) {
        return SDL_OutOfMemory();
    }
    SDL_memset(haptic->effects, 0, sizeof(struct haptic_effect) * haptic->neffects);

    haptic->hwdata = (struct haptic_hwdata *)&haptic_devices[index];
    haptic_devices[index].haptic = haptic;

    SDL_Log("Haptic device initialized successfully: Rumble supported!");
    return 0;
}

int SDL_SYS_HapticInit(void)
{
    int num_haptics = 0;
    maple_device_t *dev;
    SDL_Log("Initializing dreamcast haptic devices...\n");
    /* Scan for jump packs */
    for (int i = 0; i < MAX_HAPTIC_DEVICES; i++) {
        dev = maple_enum_type(i, MAPLE_FUNC_PURUPURU); /* Check for rumble */
        if (dev) {
            haptic_devices[num_haptics].device = dev;
            num_haptics++;
        }
    }
    return num_haptics;
}

int SDL_SYS_NumHaptics(void)
{
    int count = 0;
    for (int i = 0; i < MAX_HAPTIC_DEVICES; i++) {
        if (haptic_devices[i].device) count++;
    }
    return count;
}

const char *SDL_SYS_HapticName(int index)
{
    if (index >= MAX_HAPTIC_DEVICES || !haptic_devices[index].device) {
        SDL_SetError("Invalid haptic device index.");
        return NULL;
    }
    return "Dreamcast Jump Pack";
}

int SDL_SYS_HapticOpen(SDL_Haptic *haptic)
{
    if (!haptic || haptic->index >= MAX_HAPTIC_DEVICES || !haptic_devices[haptic->index].device) {
        return SDL_SetError("Invalid haptic device.");
    }
    return DREAMCAST_InitHapticHandle(haptic, haptic->index);
}

int SDL_SYS_HapticMouse(void)
{
    return -1; /* No mouse haptic support */
}

int SDL_SYS_JoystickIsHaptic(SDL_Joystick *joystick)
{
    return (joystick->instance_id < MAX_HAPTIC_DEVICES && haptic_devices[joystick->instance_id].device) ? 1 : 0;
}

int SDL_SYS_HapticOpenFromJoystick(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    if (!joystick || !haptic || joystick->instance_id >= MAX_HAPTIC_DEVICES) {
        return SDL_SetError("Invalid joystick for haptic.");
    }

    if (!haptic_devices[joystick->instance_id].device) {
        return SDL_SetError("No haptic device found for joystick.");
    }

    return DREAMCAST_InitHapticHandle(haptic, joystick->instance_id);
}

int SDL_SYS_JoystickSameHaptic(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    if (!haptic || !joystick) {
        return 0;
    }

    return (haptic->index == joystick->instance_id);
}

/*
 * Runs a haptic effect on the Dreamcast.
 */
int SDL_SYS_HapticRunEffect(SDL_Haptic *haptic, struct haptic_effect *effect, Uint32 iterations)
{
    haptic_hwdata *hwdata;
    maple_device_t *dev;

    if (!haptic || !haptic->hwdata) return SDL_SetError("Invalid haptic device.");

    hwdata = (haptic_hwdata *)haptic->hwdata;
    dev = hwdata->device;
    if (!dev) return SDL_SetError("Haptic device not found.");

    // Mark the effect as running
    if (effect && effect->hweffect) {
        effect->hweffect->is_running = 1;
    }

    // Start rumble
    if (iterations > 0) {
        if (effect && effect->hweffect) {
            return purupuru_rumble(dev, &effect->hweffect->rumble);
        }
        return SDL_SetError("Haptic effect data missing.");
    } else {
        return SDL_SYS_HapticStopEffect(haptic, effect);
    }
}

/*
 * Stops a haptic effect on the Dreamcast.
 */
int SDL_SYS_HapticStopEffect(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    haptic_hwdata *hwdata;
    maple_device_t *dev;

    if (!haptic || !haptic->hwdata) return SDL_SetError("Invalid haptic device.");

    hwdata = (haptic_hwdata *)haptic->hwdata;
    dev = hwdata->device;
    if (!dev) return SDL_SetError("Haptic device not found.");

    // Mark the effect as not running
    if (effect && effect->hweffect) {
        effect->hweffect->is_running = 0;
    }

    // Stop rumble
    return purupuru_rumble(dev, &(const purupuru_effect_t){ .motor = 1 });
}

void SDL_SYS_HapticDestroyEffect(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    // SDL_SYS_LogicError();
    return;
}

void SDL_SYS_HapticClose(SDL_Haptic *haptic)
{
    if (haptic) {
        haptic->hwdata = NULL;
    }
}

void SDL_SYS_HapticQuit(void)
{
    for (int i = 0; i < MAX_HAPTIC_DEVICES; i++) {
        haptic_devices[i].device = NULL;
    }
}

/*
 * Creates a new haptic effect for the Dreamcast.
 */
int SDL_SYS_HapticNewEffect(SDL_Haptic *haptic, struct haptic_effect *effect, SDL_HapticEffect *base)
{
    // Validate input
    if (!haptic || !effect || !base) {
        return SDL_SetError("Invalid haptic device or effect.");
    }

    // Ensure the effect type is supported (only SDL_HAPTIC_LEFTRIGHT is supported for rumble)
    if (base->type != SDL_HAPTIC_LEFTRIGHT) {
        return SDL_SetError("Unsupported haptic effect type.");
    }

    // Allocate memory for the hardware-specific effect data
    effect->hweffect = (struct haptic_hweffect *)SDL_malloc(sizeof(struct haptic_hweffect));
    if (!effect->hweffect) {
        return SDL_OutOfMemory();
    }
    SDL_memset(effect->hweffect, 0, sizeof(struct haptic_hweffect));

    // Store the effect parameters
    effect->hweffect->effect = *base; // Copy the base effect data
    effect->hweffect->is_running = 0; // Initialize the effect as not running

    // Log success
    SDL_Log("Haptic effect created successfully.");

    return 0;
}

int SDL_SYS_HapticUpdateEffect(SDL_Haptic *haptic, struct haptic_effect *effect, SDL_HapticEffect *data)
{
    SDL_HapticLeftRight *leftright;
    uint16_t length;
    uint16_t intensity;
    haptic_hwdata *hwdata;
    maple_device_t *dev;

    if (!haptic || !effect || !data) {
        return SDL_SetError("Haptic: Invalid parameters.");
    }

    if (data->type != SDL_HAPTIC_LEFTRIGHT) {
        return SDL_SetError("Haptic: Unsupported effect type.");
    }

    leftright = &data->leftright;
    length = leftright->length;
    intensity = (uint16_t)(((uint32_t)leftright->large_magnitude + (uint32_t)leftright->small_magnitude) / 2u);

    hwdata = (haptic_hwdata *)haptic->hwdata;
    dev = hwdata->device;

    DREAMCAST_BuildRumblePacket(&effect->hweffect->rumble, intensity, length);

    if (purupuru_rumble(dev, &effect->hweffect->rumble) < 0) {
        return SDL_SetError("Haptic: Failed to update effect.");
    }

    // Store the effect data into the hweffect struct
    effect->hweffect->intensity = intensity;
    effect->hweffect->length = length;

    return 0;
}


int SDL_SYS_HapticGetEffectStatus(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    return 0;
}

int SDL_SYS_HapticSetGain(SDL_Haptic *haptic, int gain)
{
    return 0; /* Gain control not implemented */
}

int SDL_SYS_HapticSetAutocenter(SDL_Haptic *haptic, int autocenter)
{
    return 0;
}

int SDL_SYS_HapticPause(SDL_Haptic *haptic)
{
    return SDL_SYS_HapticStopAll(haptic);
}

int SDL_SYS_HapticUnpause(SDL_Haptic *haptic)
{
    return 0;
}

int SDL_SYS_HapticStopAll(SDL_Haptic *haptic)
{
    return SDL_SYS_HapticStopEffect(haptic, NULL);
}

#endif /* SDL_HAPTIC_DREAMCAST */
