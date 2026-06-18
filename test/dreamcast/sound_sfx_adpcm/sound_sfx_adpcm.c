/*
 * Dreamcast ADPCM SFX sample.
 *
 * This loads Dreamcast ADPCM WAV files with SDL_LoadDreamcastADPCM_IO() and
 * feeds them into SDL_AudioStream. On Dreamcast, the backend uses the stream
 * property to route those loaded buffers through the KOS sound-effect
 * manager.
*/

#include <SDL3/SDL.h>
#include <stdio.h>

#ifdef SDL_PLATFORM_DREAMCAST
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

typedef struct SfxClip
{
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
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
static SDL_AudioStream *stream = NULL;

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
        if (clips[i].buf) {
            SDL_free(clips[i].buf);
            clips[i].buf = NULL;
        }
    }
}

static int load_clip(int idx)
{
    SDL_IOStream *io = SDL_IOFromFile(clip_paths[idx], "rb");

    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open %s: %s", clip_paths[idx], SDL_GetError());
        return -1;
    }

    if (!SDL_LoadDreamcastADPCM_IO(io, true, &clips[idx].spec, &clips[idx].buf, &clips[idx].len)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", clip_paths[idx], SDL_GetError());
        return -1;
    }

    clips[idx].name = clip_names[idx];

    SDL_Log("Loaded %s: rate=%d channels=%d len=%" SDL_PRIu32,
            clips[idx].name, clips[idx].spec.freq, clips[idx].spec.channels, clips[idx].len);
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

static int open_audio(void)
{
    SDL_AudioSpec desired = clips[0].spec;

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, NULL, NULL);
    if (!stream) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open audio stream: %s", SDL_GetError());
        return -1;
    }

    if (!SDL_SetBooleanProperty(SDL_GetAudioStreamProperties(stream), SDL_PROP_AUDIOSTREAM_DREAMCAST_ADPCM_SFX_BOOLEAN, true)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't enable Dreamcast SFX fast-path: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        stream = NULL;
        return -1;
    }

    SDL_ResumeAudioStreamDevice(stream);
    return 0;
}

static void play_clip(int idx)
{
    if (!SDL_PutAudioStreamData(stream, clips[idx].buf, clips[idx].len)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't play %s: %s", clips[idx].name, SDL_GetError());
    }
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

    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /* Keep the Dreamcast backend on its raw ADPCM stream path. */
    SDL_SetHint(SDL_HINT_AUDIO_ADPCM_STREAM_DC, "1");

    vid_set_mode(DM_640x480, PM_RGB555);

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL audio: %s", SDL_GetError());
        return 1;
    }

    if (load_clips() < 0) {
        free_clips();
        SDL_Quit();
        return 1;
    }

    if (open_audio() < 0) {
        free_clips();
        SDL_Quit();
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
            play_clip(0);
        }
        if (button_pressed(current_buttons, changed_buttons, CONT_B)) {
            play_clip(1);
        }
        if (button_pressed(current_buttons, changed_buttons, CONT_X)) {
            play_clip(2);
        }
        if (button_pressed(current_buttons, changed_buttons, CONT_Y)) {
            play_clip(3);
        }

        if (button_pressed(current_buttons, changed_buttons, CONT_START)) {
            break;
        }
    }

    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
    free_clips();
    SDL_Quit();
    return 0;
}
#endif /* SDL_PLATFORM_DREAMCAST */
