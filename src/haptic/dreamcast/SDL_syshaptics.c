/*
  SDL Dreamcast Haptic (Rumble) Driver
  Based on SDL haptic dummy driver, modified for Dreamcast Jump Pack support.
*/

#include "../../SDL_internal.h"

#ifdef SDL_HAPTIC_DREAMCAST

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/purupuru.h>

#include "SDL_haptic.h"
#include "../SDL_syshaptic.h"
#include "../../joystick/SDL_sysjoystick.h"
#include "../../joystick/SDL_joystick_c.h"

#define MAX_HAPTIC_DEVICES 4

typedef struct haptic_hwdata
{
    SDL_JoystickID instance_id;
    maple_device_t *device;
    SDL_Haptic *haptic;
} haptic_hwdata;

struct haptic_hweffect
{
    SDL_HapticEffect effect;
    int is_running;
    uint8_t intensity;
    uint16_t length;
    uint32_t raw;
    int pattern_index;
};

typedef struct
{
    uint32_t pattern;
    const char *description;
} baked_pattern_t;

static const baked_pattern_t catalog[] = {
    { 0x011A7010, "Basic Thud (.5s jolt)" },
    { 0x31071011, "Car Idle (69 Mustang)" },
    { 0x2615F010, "Car Idle (VW Beetle)" },
    { 0x3339F010, "Earthquake (fade out)" },
    { 0x05281011, "Helicopter" },
    { 0x00072010, "Ship's Thrust (AAC)" }
};

typedef union rumble_fields
{
    uint32_t raw;
    struct {
        uint32_t special_pulse    : 1;
        uint32_t                  : 3;
        uint32_t special_motor1   : 1;
        uint32_t                  : 2;
        uint32_t special_motor2   : 1;
        uint32_t fx1_powersave    : 4;
        uint32_t fx1_intensity    : 3;
        uint32_t fx1_pulse        : 1;
        uint32_t fx2_lintensity   : 3;
        uint32_t fx2_pulse        : 1;
        uint32_t fx2_uintensity   : 3;
        uint32_t fx2_decay        : 1;
        uint32_t duration         : 8;
    };
} rumble_fields_t;

static haptic_hwdata haptic_devices[MAX_HAPTIC_DEVICES] = { { 0, NULL, NULL } };
static int num_haptics = 0;

static haptic_hwdata *DC_HapticByInstanceID(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_HAPTIC_DEVICES; ++i) {
        if (haptic_devices[i].device && haptic_devices[i].instance_id == id) {
            return &haptic_devices[i];
        }
    }
    return NULL;
}

static Uint8 DREAMCAST_ClampRumbleLevel(Uint16 magnitude)
{
    Uint32 level = ((Uint32)magnitude * 7u + 16383u) / 32767u;
    if (level > 7u) {
        level = 7u;
    }
    if (level == 0u && magnitude > 0u) {
        level = 1u;
    }
    return (Uint8)level;
}

static Uint32 DREAMCAST_BuildRumblePattern(Uint16 magnitude)
{
    rumble_fields_t rf;
    Uint8 scaled;

    SDL_memset(&rf, 0, sizeof(rf));
    scaled = DREAMCAST_ClampRumbleLevel(magnitude);
    if (scaled == 0) {
        return 0;
    }

    rf.special_motor1 = 1;
    rf.duration = 255;
    rf.fx1_intensity = scaled;
    rf.fx2_lintensity = scaled;
    rf.fx2_uintensity = scaled;

    if (scaled >= 6) {
        rf.special_pulse = 1;
        rf.fx1_pulse = 1;
        rf.fx2_pulse = 1;
    } else if (scaled >= 3) {
        rf.fx2_decay = 1;
    }

    return rf.raw;
}

static Uint32 DREAMCAST_BuildRumblePatternDual(Uint16 left, Uint16 right)
{
    Uint16 magnitude = (Uint16)(((Uint32)left + (Uint32)right) / 2u);
    return DREAMCAST_BuildRumblePattern(magnitude);
}

static int DREAMCAST_InitHapticHandle(SDL_Haptic *haptic, haptic_hwdata *hw)
{
    haptic->supported = SDL_HAPTIC_CONSTANT |
                        SDL_HAPTIC_LEFTRIGHT |
                        SDL_HAPTIC_SINE |
                        SDL_HAPTIC_SAWTOOTHUP;
    haptic->neffects = 5;
    haptic->nplaying = 1;
    haptic->effects = (struct haptic_effect *)SDL_calloc((size_t)haptic->neffects, sizeof(struct haptic_effect));
    if (!haptic->effects) {
        return SDL_OutOfMemory();
    }

    haptic->hwdata = (struct haptic_hwdata *)hw;
    hw->haptic = haptic;

    SDL_Log("Haptic device initialized successfully: Rumble supported!");
    return 0;
}

int SDL_SYS_HapticInit(void)
{
    num_haptics = 0;
    SDL_Log("Initializing dreamcast haptic devices...");

    for (int i = 0; i < MAX_HAPTIC_DEVICES; ++i) {
        maple_device_t *dev = maple_enum_type(i, MAPLE_FUNC_PURUPURU);
        if (dev) {
            haptic_devices[num_haptics].device = dev;
            haptic_devices[num_haptics].haptic = NULL;
            haptic_devices[num_haptics].instance_id = SDL_GetNextJoystickInstanceID();
            ++num_haptics;
        }
    }

    return num_haptics;
}

int SDL_SYS_NumHaptics(void)
{
    return num_haptics;
}

const char *SDL_SYS_HapticName(int index)
{
    if (index < 0 || index >= num_haptics || !haptic_devices[index].device) {
        SDL_SetError("Invalid haptic device index.");
        return NULL;
    }

    return haptic_devices[index].device->info.product_name;
}

int SDL_SYS_HapticOpen(SDL_Haptic *haptic)
{
    haptic_hwdata *hw;

    if (!haptic || haptic->index < 0 || haptic->index >= num_haptics) {
        return SDL_SetError("Invalid haptic device.");
    }

    hw = &haptic_devices[haptic->index];
    if (!hw->device) {
        return SDL_SetError("Haptic device not found.");
    }

    return DREAMCAST_InitHapticHandle(haptic, hw);
}

int SDL_SYS_HapticMouse(void)
{
    return -1;
}

int SDL_SYS_JoystickIsHaptic(SDL_Joystick *joystick)
{
    SDL_JoystickID instance_id;

    if (!joystick) {
        return 0;
    }

    instance_id = SDL_JoystickInstanceID(joystick);
    return (DC_HapticByInstanceID(instance_id) != NULL) ? 1 : 0;
}

int SDL_SYS_HapticOpenFromJoystick(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    haptic_hwdata *hw;
    SDL_JoystickID instance_id;

    if (!joystick || !haptic) {
        return SDL_SetError("Invalid joystick for haptic.");
    }

    instance_id = SDL_JoystickInstanceID(joystick);
    hw = DC_HapticByInstanceID(instance_id);
    if (!hw || !hw->device) {
        return SDL_SetError("Invalid joystick instance ID for haptics.");
    }

    return DREAMCAST_InitHapticHandle(haptic, hw);
}

int SDL_SYS_JoystickSameHaptic(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    if (!haptic || !joystick || !haptic->hwdata) {
        return 0;
    }

    return (((haptic_hwdata *)haptic->hwdata)->instance_id == SDL_JoystickInstanceID(joystick)) ? 1 : 0;
}

int SDL_SYS_HapticRunEffect(SDL_Haptic *haptic, struct haptic_effect *effect, Uint32 iterations)
{
    haptic_hwdata *hwdata;
    maple_device_t *dev;
    struct haptic_hweffect *hw_effect;

    if (!haptic || !haptic->hwdata || !effect || !effect->hweffect) {
        return SDL_SetError("Invalid haptic or effect data.");
    }

    hwdata = (haptic_hwdata *)haptic->hwdata;
    dev = hwdata->device;
    if (!dev || !dev->valid) {
        return SDL_SetError("Invalid device.");
    }

    hw_effect = (struct haptic_hweffect *)effect->hweffect;
    if (iterations == 0) {
        return SDL_SYS_HapticStopEffect(haptic, effect);
    }

    if (hw_effect->raw) {
        if (purupuru_rumble_raw(dev, hw_effect->raw) < 0) {
            return SDL_SetError("Failed to send rumble command");
        }
        hw_effect->is_running = 1;
        return 0;
    }

    return SDL_SYS_HapticStopEffect(haptic, effect);
}

int SDL_SYS_HapticStopEffect(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    haptic_hwdata *hwdata;
    maple_device_t *dev;

    if (!haptic || !haptic->hwdata) {
        return SDL_SetError("Invalid haptic device.");
    }

    hwdata = (haptic_hwdata *)haptic->hwdata;
    dev = hwdata->device;
    if (!dev) {
        return SDL_SetError("No haptic device.");
    }

    if (effect && effect->hweffect) {
        ((struct haptic_hweffect *)effect->hweffect)->is_running = 0;
    }

    return (purupuru_rumble_raw(dev, 0) == 0) ? 0 : SDL_SetError("Failed to stop haptic effect.");
}

void SDL_SYS_HapticDestroyEffect(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    if (effect && effect->hweffect) {
        SDL_free(effect->hweffect);
        effect->hweffect = NULL;
    }
}

void SDL_SYS_HapticClose(SDL_Haptic *haptic)
{
    if (haptic) {
        if (haptic->hwdata) {
            ((haptic_hwdata *)haptic->hwdata)->haptic = NULL;
        }
        haptic->hwdata = NULL;
    }
}

void SDL_SYS_HapticQuit(void)
{
    for (int i = 0; i < MAX_HAPTIC_DEVICES; ++i) {
        haptic_devices[i].device = NULL;
        haptic_devices[i].haptic = NULL;
        haptic_devices[i].instance_id = 0;
    }
    num_haptics = 0;
}

int SDL_SYS_HapticNewEffect(SDL_Haptic *haptic, struct haptic_effect *effect, SDL_HapticEffect *base)
{
    struct haptic_hweffect *hw_effect;

    if (!haptic || !effect || !base) {
        return SDL_SetError("Invalid effect creation parameters.");
    }

    hw_effect = (struct haptic_hweffect *)SDL_calloc(1, sizeof(struct haptic_hweffect));
    if (!hw_effect) {
        return SDL_OutOfMemory();
    }

    hw_effect->effect = *base;
    hw_effect->is_running = 0;
    hw_effect->intensity = 0;
    hw_effect->length = 0;
    hw_effect->raw = 0;
    hw_effect->pattern_index = -1;

    switch (base->type) {
    case SDL_HAPTIC_CONSTANT:
        hw_effect->pattern_index = 3;
        hw_effect->raw = catalog[3].pattern;
        break;

    case SDL_HAPTIC_LEFTRIGHT:
        hw_effect->raw = DREAMCAST_BuildRumblePatternDual(base->leftright.large_magnitude,
                                                          base->leftright.small_magnitude);
        if (!hw_effect->raw) {
            hw_effect->pattern_index = 4;
            hw_effect->raw = catalog[4].pattern;
        }
        break;

    case SDL_HAPTIC_SINE:
        hw_effect->pattern_index = 3;
        hw_effect->raw = catalog[3].pattern;
        break;

    case SDL_HAPTIC_SAWTOOTHUP:
        hw_effect->pattern_index = 4;
        hw_effect->raw = catalog[4].pattern;
        break;

    default:
        SDL_SetError("Unsupported effect type.");
        SDL_free(hw_effect);
        return -1;
    }

    effect->hweffect = hw_effect;
    return 0;
}

int SDL_SYS_HapticUpdateEffect(SDL_Haptic *haptic, struct haptic_effect *effect, SDL_HapticEffect *data)
{
    struct haptic_hweffect *hw_effect;

    if (!haptic || !effect || !data || !effect->hweffect) {
        return SDL_SetError("Invalid update parameters.");
    }

    hw_effect = (struct haptic_hweffect *)effect->hweffect;
    hw_effect->effect = *data;
    hw_effect->pattern_index = -1;

    switch (data->type) {
    case SDL_HAPTIC_CONSTANT:
        hw_effect->intensity = (Uint8)SDL_min(SDL_abs(data->constant.level), 255);
        hw_effect->length = data->constant.length ? data->constant.length : 1000;
        hw_effect->raw = DREAMCAST_BuildRumblePattern((Uint16)SDL_abs(data->constant.level));
        break;

    case SDL_HAPTIC_LEFTRIGHT:
        hw_effect->intensity = (Uint8)SDL_min(((Uint32)data->leftright.large_magnitude +
                                               (Uint32)data->leftright.small_magnitude) / 2u, 255u);
        hw_effect->length = data->leftright.length ? data->leftright.length : 1000;
        hw_effect->raw = DREAMCAST_BuildRumblePatternDual(data->leftright.large_magnitude,
                                                          data->leftright.small_magnitude);
        break;

    case SDL_HAPTIC_SINE:
        hw_effect->intensity = (Uint8)SDL_min(SDL_abs(data->periodic.magnitude), 255);
        hw_effect->length = data->periodic.length ? data->periodic.length : 1000;
        hw_effect->raw = catalog[3].pattern;
        hw_effect->pattern_index = 3;
        break;

    case SDL_HAPTIC_SAWTOOTHUP:
        hw_effect->intensity = (Uint8)SDL_min(SDL_abs(data->periodic.magnitude), 255);
        hw_effect->length = data->periodic.length ? data->periodic.length : 1000;
        hw_effect->raw = catalog[4].pattern;
        hw_effect->pattern_index = 4;
        break;

    default:
        SDL_SetError("Unsupported effect type.");
        return -1;
    }

    return 0;
}

int SDL_SYS_HapticGetEffectStatus(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    return (effect && effect->hweffect) ? ((struct haptic_hweffect *)effect->hweffect)->is_running : 0;
}

int SDL_SYS_HapticSetGain(SDL_Haptic *haptic, int gain)
{
    return 0;
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
