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
    int pattern_index;  // -1 = raw; 0+ = catalog index
    const char *description_override;
};

typedef struct {
    uint32_t pattern;
    const char *description;
} baked_pattern_t;

static const baked_pattern_t catalog[] = {
    {0x011A7010, "Basic Thud (.5s jolt)"},
    {0x31071011, "Car Idle (69 Mustang)"},
    {0x2615F010, "Car Idle (VW Beetle)"},
    {0x3339F010, "Earthquake (fade out)"},
    {0x05281011, "Helicopter"},
    {0x00072010, "Ship's Thrust (AAC)"}
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
    haptic_hwdata *hw = DC_HapticByInstanceID(haptic->instance_id);
    if (!hw || !hw->device) {
        SDL_SetError("Haptic device not found.");
        return false;
    }

    haptic->hwdata = hw;
    hw->haptic = haptic;

    haptic->supported = SDL_HAPTIC_CONSTANT |
                        SDL_HAPTIC_LEFTRIGHT |
                        SDL_HAPTIC_SINE |
                        SDL_HAPTIC_SAWTOOTHUP |
                        SDL_HAPTIC_SQUARE;

    haptic->neffects = 5;
    haptic->nplaying = 1;
    haptic->effects = SDL_calloc(haptic->neffects, sizeof(struct haptic_effect));
    return haptic->effects != NULL;
}





int SDL_SYS_HapticMouse(void)
{
    return -1;
}


bool SDL_SYS_JoystickIsHaptic(SDL_Joystick *joystick)
{
    SDL_JoystickID instance_id = SDL_GetJoystickID(joystick);
    return (instance_id >= 0 && instance_id < MAX_HAPTIC_DEVICES &&
            haptic_devices[instance_id].device != NULL);
}

bool SDL_SYS_HapticOpenFromJoystick(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    SDL_JoystickID instance_id = SDL_GetJoystickID(joystick);
    if (instance_id < 0 || instance_id >= MAX_HAPTIC_DEVICES || !haptic_devices[instance_id].device) {
        SDL_SetError("Invalid joystick instance ID for haptics.");
        return false;
    }

    haptic->hwdata = &haptic_devices[instance_id];
    haptic_devices[instance_id].haptic = haptic;
    return true;
}


bool SDL_SYS_JoystickSameHaptic(SDL_Haptic *haptic, SDL_Joystick *joystick)
{
    return false;
}

static inline uint32_t build_rumble_pattern_from_intensity(Uint16 intensity)
{
    rumble_fields_t rf = { .raw = 0 };

    // Always use motor1 (most jump packs support only motor1)
    rf.special_motor1 = 1;

    // Max duration
    rf.duration = 255;

    // Convert SDL 0-32767 scale into 1–7 (or 0–7 if you want full range)
    int scaled = (intensity * 7 + 16383) / 32767;
    if (scaled > 7) scaled = 7;

    // Use scaled intensity for both parts
    rf.fx1_intensity = scaled;
    rf.fx2_lintensity = scaled;
    rf.fx2_uintensity = scaled;

    // Add pulse effects for higher intensities
    if (scaled >= 6) {
        rf.fx1_pulse = 1;
        rf.fx2_pulse = 1;
        rf.special_pulse = 1;
    }

    // Optional: decay effect for low-to-mid
    if (scaled >= 3 && scaled < 6) {
        rf.fx2_decay = 1;
    }

    return rf.raw;
}

uint32_t build_rumble_pattern_dual_motor(Uint16 left, Uint16 right)
{
    rumble_fields_t f = { .raw = 0 };

    // Normalize to 0-7 range (valid intensity range)
    int lval = (left * 7 + 16384) / 32767;
    int rval = (right * 7 + 16384) / 32767;

    f.special_motor1 = 1;
    f.fx1_intensity = lval;
    f.fx2_lintensity = rval;
    f.duration = 255;

    return f.raw;
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

    if (hw_effect->pattern_index >= 0 && hw_effect->pattern_index < NUM_BAKED_PATTERNS) {
        uint32_t pattern = catalog[hw_effect->pattern_index].pattern;
        if (purupuru_rumble_raw(dev, pattern) < 0) {
            SDL_SetError("Failed to send pattern rumble command");
            return false;
        }
        hw_effect->is_running = 1;
        return true;
    }

    Uint16 intensity = 32767;
    uint32_t raw = 0;

    if (hw_effect->effect.type == SDL_HAPTIC_CONSTANT) {
        intensity = SDL_abs(hw_effect->effect.constant.level);
        if (intensity > 32767) intensity = 32767;

        // Optional boost for low values
        if (intensity > 0 && intensity < 8000) {
            intensity = 8000;
        }
        raw = build_rumble_pattern_from_intensity(intensity);
    } else if (hw_effect->effect.type == SDL_HAPTIC_LEFTRIGHT) {
        Uint16 left = hw_effect->effect.leftright.large_magnitude;
        Uint16 right = hw_effect->effect.leftright.small_magnitude;
        raw = build_rumble_pattern_dual_motor(left, right);
    } else {
        // fallback to default intensity if unsupported type
        raw = build_rumble_pattern_from_intensity(intensity);
    }

    if (purupuru_rumble_raw(dev, raw) < 0) {
        SDL_SetError("Failed to send rumble command");
        return false;
    }

    hw_effect->intensity = intensity;
    hw_effect->length = 255;
    hw_effect->is_running = 1;

    return true;
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
        haptic->hwdata = NULL;
    }
}

void SDL_SYS_HapticQuit(void)
{
    for (int i = 0; i < MAX_HAPTIC_DEVICES; ++i) {
        haptic_devices[i].device = NULL;
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
    effect->hweffect->intensity = 128;  // midpoint (~0.5 strength), fits in uint8_t

    switch (base->type) {
    case SDL_HAPTIC_CONSTANT:
        effect->hweffect->pattern_index = 3;  // "Earthquake"
        effect->hweffect->description_override = "Emulated Constant (Sine)";
        break;

    case SDL_HAPTIC_LEFTRIGHT:
        effect->hweffect->pattern_index = 4;  // "Helicopter"
        effect->hweffect->description_override = "Emulated Left/Right (Sawtooth)";
        break;

    case SDL_HAPTIC_SINE:
        effect->hweffect->pattern_index = 3;
        effect->hweffect->description_override = catalog[3].description;
        break;

    case SDL_HAPTIC_SQUARE:
        effect->hweffect->pattern_index = 0;
        effect->hweffect->description_override = catalog[0].description;
        break;

    case SDL_HAPTIC_SAWTOOTHUP:
        effect->hweffect->pattern_index = 4;
        effect->hweffect->description_override = catalog[4].description;
        break;

    case SDL_HAPTIC_CUSTOM:
        effect->hweffect->pattern_index = 1;
        effect->hweffect->description_override = catalog[1].description;
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
    Uint16 intensity = 0;
    Uint32 duration = 1000;

    // if (data->type == SDL_HAPTIC_CONSTANT) {
    //     duration = data->constant.length ? data->constant.length : 1000;
    //     intensity = SDL_abs(data->constant.level);
    //     if (intensity < 8000 && intensity > 0) intensity = 8000;
    // } else if (data->type == SDL_HAPTIC_LEFTRIGHT) {
        intensity = (data->leftright.large_magnitude + data->leftright.small_magnitude) / 2;
        // duration = data->leftright.length ? data->leftright.length : 1000;
    // } else {
    //     SDL_SetError("Unsupported effect type");
    //     return false;
    // }

    uint32_t raw;

    // if (data->type == SDL_HAPTIC_LEFTRIGHT) {
    //     Uint16 left = data->leftright.large_magnitude;
    //     Uint16 right = data->leftright.small_magnitude;
    //     raw = build_rumble_pattern_dual_motor(left, right);
    // } else {
        raw = build_rumble_pattern_from_intensity(intensity);
    // }
    Uint32 start = SDL_GetTicks();
    Uint32 now = start;

    while ((now - start) < duration) {
        purupuru_rumble_raw(hwdata->device, raw);
        SDL_Delay(100);  // re-send every 100ms to keep it alive
        now = SDL_GetTicks();
    }

    purupuru_rumble_raw(hwdata->device, 0);  // stop rumble

    if (effect->hweffect) {
        effect->hweffect->intensity = intensity;
        effect->hweffect->length = duration;
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
