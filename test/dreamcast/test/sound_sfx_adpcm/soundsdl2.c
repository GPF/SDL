/*
 * Dreamcast ADPCM SFX sample.
 *
 * This loads Dreamcast ADPCM WAV files with SDL_LoadDreamcastADPCM_RW() and
 * hands the raw buffers to the KOS sound-effect manager for per-channel SFX
 * playback.
 */

#include <stdio.h>
#include <stdlib.h>
#include "SDL2/SDL.h"

#ifdef DREAMCAST
#include "kos.h"
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);

typedef struct SfxClip
{
    Uint8 *buf;
    Uint32 len;
    uint32_t rate;
    uint16_t channels;
    sfxhnd_t handle;
    const char *name;
} SfxClip;

static const char *clip_paths[] = {
    "/rd/beep-1_adpcm.wav",
    "/rd/beep-2_adpcm.wav",
    "/rd/beep-3_adpcm.wav",
    "/rd/beep-4_adpcm.wav",
};

static const char *clip_names[] = {
    "beep-1",
    "beep-2",
    "beep-3",
    "beep-4",
};

static SfxClip clips[4];

static cont_state_t *get_cont_state(void)
{
    maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (cont) {
        return (cont_state_t *)maple_dev_status(cont);
    }
    return NULL;
}

static int button_pressed(uint32_t current_buttons, uint32_t changed_buttons, uint32_t button)
{
    return (changed_buttons & button) && (current_buttons & button);
}

static void free_clips(void)
{
    for (int i = 0; i < 4; i++) {
        if (clips[i].handle != SFXHND_INVALID) {
            snd_sfx_unload(clips[i].handle);
            clips[i].handle = SFXHND_INVALID;
        }
        if (clips[i].buf) {
            SDL_free(clips[i].buf);
            clips[i].buf = NULL;
        }
    }
}

static int load_clip(int idx)
{
    SDL_AudioSpec spec;
    SDL_RWops *rw = SDL_RWFromFile(clip_paths[idx], "rb");
    if (!rw) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open %s: %s", clip_paths[idx], SDL_GetError());
        return -1;
    }

    if (SDL_LoadDreamcastADPCM_RW(rw, 1, &spec, &clips[idx].buf, &clips[idx].len) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", clip_paths[idx], SDL_GetError());
        return -1;
    }

    clips[idx].rate = (uint32_t)spec.freq;
    clips[idx].channels = (uint16_t)spec.channels;
    clips[idx].name = clip_names[idx];

    clips[idx].handle = snd_sfx_load_raw_buf((char *)clips[idx].buf, clips[idx].len, clips[idx].rate, 4, clips[idx].channels);
    if (clips[idx].handle == SFXHND_INVALID) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "snd_sfx_load_raw_buf failed for %s", clips[idx].name);
        return -1;
    }

    SDL_Log("Loaded %s: rate=%" SDL_PRIu32 " channels=%u len=%" SDL_PRIu32, clips[idx].name, clips[idx].rate, clips[idx].channels, clips[idx].len);
    return 0;
}

static int load_clips(void)
{
    for (int i = 0; i < 4; i++) {
        if (load_clip(i) < 0) {
            return -1;
        }
    }
    return 0;
}

static void draw_instructions(void)
{
    printf("Press A/B/X/Y to play beep-1/beep-2/beep-3/beep-4\n");
    printf("Press Start to exit\n");
}

int main(int argc, char **argv)
{
    cont_state_t *cond;
    uint32_t current_buttons = 0;
    uint32_t changed_buttons = 0;
    uint32_t previous_buttons = 0;

    (void)argc;
    (void)argv;

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    vid_set_mode(DM_640x480, PM_RGB555);
    snd_init();

    if (load_clips() < 0) {
        free_clips();
        snd_shutdown();
        return 1;
    }

    draw_instructions();

    for (;;) {
        if (!(cond = get_cont_state())) {
            continue;
        }

        current_buttons = cond->buttons;
        changed_buttons = current_buttons ^ previous_buttons;
        previous_buttons = current_buttons;

        if (button_pressed(current_buttons, changed_buttons, CONT_A)) {
            snd_sfx_play(clips[0].handle, 255, 128);
        }
        if (button_pressed(current_buttons, changed_buttons, CONT_B)) {
            snd_sfx_play(clips[1].handle, 255, 128);
        }
        if (button_pressed(current_buttons, changed_buttons, CONT_X)) {
            snd_sfx_play(clips[2].handle, 255, 128);
        }
        if (button_pressed(current_buttons, changed_buttons, CONT_Y)) {
            snd_sfx_play(clips[3].handle, 255, 128);
        }

        if (button_pressed(current_buttons, changed_buttons, CONT_START)) {
            break;
        }
    }

    free_clips();
    snd_shutdown();
    return 0;
}
#endif /* DREAMCAST */
