#include "SDL_internal.h"

#ifdef SDL_HAPTIC_DREAMCAST
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/purupuru.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_joystick.h>
#include "../SDL_haptic_c.h"
#include "../SDL_syshaptic.h"


#include "../../joystick/SDL_joystick_c.h"

#define MAX_HAPTIC_DEVICES 4

typedef struct haptic_hwdata {
    SDL_HapticID instance_id;         // SDL3 object ID
    maple_device_t *device;           // KOS device pointer
    SDL_Haptic *haptic;               // Back-reference (optional)
} haptic_hwdata;

struct haptic_hweffect {
    SDL_HapticEffect effect;
    int is_running;
    uint8_t intensity;
    uint16_t length;
    uint32_t raw;
    int pattern_index;
};

typedef struct {
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

typedef union rumble_fields {
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

#define NUM_BAKED_PATTERNS (sizeof(catalog) / sizeof(catalog[0]))

static haptic_hwdata haptic_devices[MAX_HAPTIC_DEVICES] = {
    { 0, NULL, NULL }  // SDL_HapticID = 0, device = NULL, haptic = NULL
};

static int num_haptics = 0;

static haptic_hwdata *DC_HapticByInstanceID(SDL_HapticID id)
{
    for (int i = 0; i < MAX_HAPTIC_DEVICES; ++i) {
        if (haptic_devices[i].instance_id == id) {
            return &haptic_devices[i];
        }
    }
    return NULL;
}

static Uint8 DREAMCAST_ClampRumbleLevel(Uint16 magnitude)
{
    uint32_t level = ((uint32_t)magnitude * 7u + 16383u) / 32767u;
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
    rumble_fields_t rf = { .raw = 0 };
    const Uint8 scaled = DREAMCAST_ClampRumbleLevel(magnitude);

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
    const Uint16 magnitude = (Uint16)(((Uint32)left + (Uint32)right) / 2u);
    return DREAMCAST_BuildRumblePattern(magnitude);
}

static bool DREAMCAST_InitHapticHandle(SDL_Haptic *haptic, haptic_hwdata *hw)
{
    haptic->supported = SDL_HAPTIC_CONSTANT |
                        SDL_HAPTIC_LEFTRIGHT |
                        SDL_HAPTIC_SINE |
                        SDL_HAPTIC_SAWTOOTHUP |
                        SDL_HAPTIC_SQUARE;
    haptic->neffects = 5;
    haptic->nplaying = 1;
    haptic->effects = SDL_calloc(haptic->neffects, sizeof(struct haptic_effect));
    if (!haptic->effects) {
        return false;
    }

    haptic->hwdata = hw;
    hw->haptic = haptic;
    return true;
}

bool SDL_SYS_HapticInit(void)
{
    num_haptics = 0;
    for (int i = 0; i < MAX_HAPTIC_DEVICES; ++i) {
        maple_device_t *dev = maple_enum_type(i, MAPLE_FUNC_PURUPURU);
        if (dev) {
            haptic_devices[num_haptics].device = dev;
            haptic_devices[num_haptics].haptic = NULL;
            haptic_devices[num_haptics].instance_id = SDL_GetNextObjectID();
            ++num_haptics;
        }
    }
    return true;
}




int SDL_SYS_NumHaptics(void)
{
    int count = 0;
    for (int i = 0; i < MAX_HAPTIC_DEVICES; i++) {
        if (haptic_devices[i].device) count++;
    }
    return count;
}

SDL_HapticID SDL_SYS_HapticInstanceID(int index)
{
    if (index < 0 || index >= MAX_HAPTIC_DEVICES || !haptic_devices[index].device) {
        return -1;
    }
    return haptic_devices[index].instance_id;  // ✅ Return real ID
}


const char *SDL_SYS_HapticName(int index)
{
    if (index < 0 || index >= MAX_HAPTIC_DEVICES || !haptic_devices[index].device) {
        SDL_SetError("Invalid haptic device index.");
        return NULL;
    }
    return "Dreamcast Jump Pack";
}

bool SDL_SYS_HapticOpen(SDL_Haptic *haptic)
{
    if (!haptic) {
        SDL_SetError("Invalid haptic device.");
        return false;
    }

    haptic_hwdata *hw = DC_HapticByInstanceID(haptic->instance_id);
    if (!hw || !hw->device) {
        SDL_SetError("Haptic device not found.");
        return false;
    }

    return DREAMCAST_InitHapticHandle(haptic, hw);
}





int SDL_SYS_HapticMouse(void)
{
    return -1;
}


bool SDL_SYS_JoystickIsHaptic(SDL_Joystick *joystick)
{
    SDL_JoystickID instance_id = SDL_GetJoystickID(joystick);
    return (DC_HapticByInstanceID(instance_id) != NULL);
}

bool SDL_SYS_HapticOpenFromJoystick(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    SDL_JoystickID instance_id = SDL_GetJoystickID(joystick);
    haptic_hwdata *hw = DC_HapticByInstanceID(instance_id);
    if (!hw || !hw->device) {
        SDL_SetError("Invalid joystick instance ID for haptics.");
        return false;
    }

    return DREAMCAST_InitHapticHandle(haptic, hw);
}


bool SDL_SYS_JoystickSameHaptic(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    return haptic && joystick && (haptic->instance_id == SDL_GetJoystickID(joystick));
}

bool SDL_SYS_HapticRunEffect(SDL_Haptic *haptic, struct haptic_effect *effect, Uint32 iterations)
{
    if (!haptic || !haptic->hwdata || !effect || !effect->hweffect) {
        SDL_SetError("Invalid haptic or effect data.");
        return false;
    }

    haptic_hwdata *hwdata = (haptic_hwdata *)haptic->hwdata;
    maple_device_t *dev = hwdata->device;
    if (!dev || !dev->valid) {
        SDL_SetError("Invalid device.");
        return false;
    }

    struct haptic_hweffect *hw_effect = (struct haptic_hweffect *)effect->hweffect;

    if (iterations == 0) {
        return SDL_SYS_HapticStopEffect(haptic, effect);
    }

    if (hw_effect->raw) {
        if (purupuru_rumble_raw(dev, hw_effect->raw) < 0) {
            SDL_SetError("Failed to send rumble command");
            return false;
        }
        hw_effect->is_running = 1;
        return true;
    }

    return SDL_SYS_HapticStopEffect(haptic, effect);
}

bool SDL_SYS_HapticStopEffect(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    if (!haptic || !haptic->hwdata) {
        SDL_SetError("Invalid haptic device.");
        return false;
    }

    haptic_hwdata *hwdata = (haptic_hwdata *)haptic->hwdata;
    maple_device_t *dev = hwdata->device;
    if (!dev) {
        SDL_SetError("No haptic device.");
        return false;
    }

    if (effect && effect->hweffect) {
        effect->hweffect->is_running = 0;
    }

    return purupuru_rumble_raw(dev, 0) == 0;
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
}

bool SDL_SYS_HapticNewEffect(SDL_Haptic *haptic, struct haptic_effect *effect, const SDL_HapticEffect *base)
{
    if (!haptic || !effect || !base) {
        SDL_SetError("Invalid effect creation parameters.");
        return false;
    }

    effect->hweffect = (struct haptic_hweffect *)SDL_calloc(1, sizeof(struct haptic_hweffect));
    if (!effect->hweffect) {
        SDL_OutOfMemory();
        return false;
    }

    effect->hweffect->effect = *base;
    effect->hweffect->length = 1000;
    effect->hweffect->intensity = 128;
    effect->hweffect->pattern_index = -1;

    switch (base->type) {
    case SDL_HAPTIC_CONSTANT:
        effect->hweffect->pattern_index = 3;
        effect->hweffect->raw = catalog[3].pattern;
        break;

    case SDL_HAPTIC_LEFTRIGHT:
        effect->hweffect->raw = DREAMCAST_BuildRumblePatternDual(base->leftright.large_magnitude,
                                                                  base->leftright.small_magnitude);
        if (!effect->hweffect->raw) {
            effect->hweffect->pattern_index = 4;
            effect->hweffect->raw = catalog[4].pattern;
        }
        break;

    case SDL_HAPTIC_SINE:
        effect->hweffect->pattern_index = 3;
        effect->hweffect->raw = catalog[3].pattern;
        break;

    case SDL_HAPTIC_SQUARE:
        effect->hweffect->pattern_index = 0;
        effect->hweffect->raw = catalog[0].pattern;
        break;

    case SDL_HAPTIC_SAWTOOTHUP:
        effect->hweffect->pattern_index = 4;
        effect->hweffect->raw = catalog[4].pattern;
        break;

    default:
        SDL_SetError("Unsupported effect type.");
        SDL_free(effect->hweffect);
        effect->hweffect = NULL;
        return false;
    }

    return true;
}



bool SDL_SYS_HapticUpdateEffect(SDL_Haptic *haptic, struct haptic_effect *effect, const SDL_HapticEffect *data)
{
    if (!haptic || !effect || !data) {
        SDL_SetError("Invalid update parameters.");
        return false;
    }

    haptic_hwdata *hwdata = (haptic_hwdata *)haptic->hwdata;
    if (!hwdata || !hwdata->device) {
        SDL_SetError("No valid device.");
        return false;
    }
    if (!effect->hweffect) {
        SDL_SetError("Invalid effect state.");
        return false;
    }

    effect->hweffect->effect = *data;
    effect->hweffect->pattern_index = -1;

    switch (data->type) {
    case SDL_HAPTIC_CONSTANT:
        effect->hweffect->intensity = (Uint8)SDL_min(SDL_abs(data->constant.level), 255);
        effect->hweffect->length = data->constant.length ? data->constant.length : 1000;
        effect->hweffect->raw = DREAMCAST_BuildRumblePattern(SDL_abs(data->constant.level));
        break;

    case SDL_HAPTIC_LEFTRIGHT:
        effect->hweffect->intensity = (Uint8)SDL_min(((Uint32)data->leftright.large_magnitude +
                                                      (Uint32)data->leftright.small_magnitude) / 2u, 255u);
        effect->hweffect->length = data->leftright.length ? data->leftright.length : 1000;
        effect->hweffect->raw = DREAMCAST_BuildRumblePatternDual(data->leftright.large_magnitude,
                                                                  data->leftright.small_magnitude);
        break;

    case SDL_HAPTIC_SINE:
        effect->hweffect->intensity = (Uint8)SDL_min(SDL_abs(data->periodic.magnitude), 255);
        effect->hweffect->length = data->periodic.length ? data->periodic.length : 1000;
        effect->hweffect->raw = catalog[3].pattern;
        effect->hweffect->pattern_index = 3;
        break;

    case SDL_HAPTIC_SQUARE:
        effect->hweffect->intensity = (Uint8)SDL_min(SDL_abs(data->periodic.magnitude), 255);
        effect->hweffect->length = data->periodic.length ? data->periodic.length : 1000;
        effect->hweffect->raw = catalog[0].pattern;
        effect->hweffect->pattern_index = 0;
        break;

    case SDL_HAPTIC_SAWTOOTHUP:
        effect->hweffect->intensity = (Uint8)SDL_min(SDL_abs(data->periodic.magnitude), 255);
        effect->hweffect->length = data->periodic.length ? data->periodic.length : 1000;
        effect->hweffect->raw = catalog[4].pattern;
        effect->hweffect->pattern_index = 4;
        break;

    default:
        SDL_SetError("Unsupported effect type.");
        return false;
    }

    return true;
}


int SDL_SYS_HapticGetEffectStatus(SDL_Haptic *haptic, struct haptic_effect *effect)
{
    return (effect && effect->hweffect && effect->hweffect->is_running);
}

bool SDL_SYS_HapticSetGain(SDL_Haptic *haptic, int gain)
{
    return true;
}

bool SDL_SYS_HapticSetAutocenter(SDL_Haptic *haptic, int autocenter)
{
    return true;
}

bool SDL_SYS_HapticPause(SDL_Haptic *haptic)
{
    return SDL_SYS_HapticStopAll(haptic);
}

bool SDL_SYS_HapticResume(SDL_Haptic *haptic)
{
    return true;
}

bool SDL_SYS_HapticStopAll(SDL_Haptic *haptic)
{
    return SDL_SYS_HapticStopEffect(haptic, NULL);
}

#endif /* SDL_HAPTIC_DREAMCAST */
