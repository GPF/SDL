
/*
 * Dreamcast ADPCM streaming sample.
 *
 * This feeds raw ADPCM bytes through the SDL audio callback path and uses
 * SDL_HINT_AUDIO_ADPCM_STREAM_DC to make the Dreamcast audio driver start
 * KOS in ADPCM streaming mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include "SDL2/SDL.h"

#ifdef DREAMCAST
#include "kos.h"
// #include "SDL_hints.h"
#define WAV_PATH "/rd/gs-16b-2c-44100hz_adpcm.wav"
extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);
#else
#define WAV_PATH "data/gs-16b-2c-44100hz.wav"
#endif

static struct {
    SDL_AudioSpec spec;
    Uint8 *sound;    /* Pointer to wave data */
    Uint32 soundlen; /* Length of wave data */
    Uint32 soundpos; /* Current play position */
} wave;

static SDL_AudioDeviceID device;

/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc) {
    SDL_Quit();
    exit(rc);
}

static void close_audio(void) {
    if (device != 0) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
}

#ifdef DREAMCAST
static int load_dreamcast_adpcm_stream(const char *filename, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len)
{
    SDL_RWops *rw;
    Uint8 header[44];
    Uint32 sampleRate;
    Uint16 channels;

    rw = SDL_RWFromFile(filename, "rb");
    if (!rw) {
        return -1;
    }

    if (SDL_RWread(rw, header, sizeof(header), 1) != 1) {
        SDL_SetError("Failed to read ADPCM header");
        SDL_RWclose(rw);
        return -1;
    }

    sampleRate = (Uint32)header[24] | ((Uint32)header[25] << 8) | ((Uint32)header[26] << 16) | ((Uint32)header[27] << 24);
    channels = (Uint16)header[22];

    if (SDL_RWseek(rw, 44, RW_SEEK_SET) < 0) {
        SDL_SetError("Failed to seek to ADPCM data");
        SDL_RWclose(rw);
        return -1;
    }

    {
        const Sint64 total_size = SDL_RWsize(rw);
        if (total_size < 44) {
            SDL_SetError("ADPCM file is too small");
            SDL_RWclose(rw);
            return -1;
        }
        *audio_len = (Uint32)(total_size - 44);
    }

    *audio_buf = (Uint8 *)SDL_malloc(*audio_len);
    if (!*audio_buf) {
        SDL_OutOfMemory();
        SDL_RWclose(rw);
        return -1;
    }

    if (SDL_RWread(rw, *audio_buf, *audio_len, 1) != 1) {
        SDL_free(*audio_buf);
        *audio_buf = NULL;
        SDL_SetError("Failed to read ADPCM data");
        SDL_RWclose(rw);
        return -1;
    }

    SDL_zero(*spec);
    spec->freq = (int)sampleRate;
    spec->format = AUDIO_S16LSB;
    spec->channels = (Uint8)channels;
    spec->samples = 512;
    spec->size = *audio_len;

    SDL_RWclose(rw);
    return 0;
}
#endif

static void open_audio(void) {
    SDL_Log("Attempting to open audio device with spec:");
    SDL_Log("  Frequency: %d", wave.spec.freq);
    SDL_Log("  Format: %d", wave.spec.format);
    SDL_Log("  Channels: %d", wave.spec.channels);
    SDL_Log("  Samples: %d", wave.spec.samples);
    SDL_Log("  soundlen: %" SDL_PRIu32, wave.soundlen);
    SDL_Log("  soundpos: %" SDL_PRIu32, wave.soundpos);

    device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &wave.spec, NULL, 0);
    if (!device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open audio: %s\n", SDL_GetError());
        SDL_FreeWAV(wave.sound);
        quit(2);
    }

    // Double-check if it's really unpaused
    SDL_PauseAudioDevice(device, 0);
    if (SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_PLAYING) {
        SDL_Log("Audio device did not start playing! %s", SDL_GetError());
    }
    SDL_Log("SDL_OpenAudioDevice successful");
}

static void reopen_audio(void) {
    close_audio();
    open_audio();
}

void SDLCALL fillerup(void *userdata, Uint8 *stream, int len) {
    (void)userdata;

    if (!wave.sound || wave.soundlen == 0) {
        SDL_memset(stream, wave.spec.silence, len);
        return;
    }

    while (len > 0) {
        const Uint32 waveleft = wave.soundlen - wave.soundpos;
        const int chunk = (waveleft < (Uint32)len) ? (int)waveleft : len;

        SDL_memcpy(stream, wave.sound + wave.soundpos, chunk);
        stream += chunk;
        len -= chunk;
        wave.soundpos += (Uint32)chunk;

        if (wave.soundpos >= wave.soundlen) {
            wave.soundpos = 0;
        }
    }
}

static int done = 0;

#ifdef __EMSCRIPTEN__
void loop(void) {
    if (done || (SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_PLAYING)) {
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int argc, char *argv[]) {
    const char *filename = WAV_PATH;
    SDL_Window *window;
    SDL_Renderer *renderer;
    #ifdef DREAMCAST
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);
    #endif
    /* Enable standard application logging */
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /* Load the SDL library */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    // Create a window
    window = SDL_CreateWindow("SDL2 Displaying Image", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create a renderer
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Log("Loading %s\n", filename);
    /* Load the audio file into memory */
#ifdef DREAMCAST
    SDL_SetHint("SDL_AUDIO_ADPCM_STREAM_DC", "1");
    if (load_dreamcast_adpcm_stream(filename, &wave.spec, &wave.sound, &wave.soundlen) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s\n", filename, SDL_GetError());
        quit(1);
    }
#else
    if (SDL_LoadWAV(filename, &wave.spec, &wave.sound, &wave.soundlen) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s\n", filename, SDL_GetError());
        quit(1);
    }
#endif

    wave.soundpos = 0;
    wave.spec.samples = 4096;
    wave.spec.callback = fillerup;

    /* Show the list of available drivers */
    SDL_Log("Available audio drivers:");
    for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
        SDL_Log("%i: %s", i, SDL_GetAudioDriver(i));
    }

    SDL_Log("Using audio driver: %s\n", SDL_GetCurrentAudioDriver());

    open_audio();

    SDL_FlushEvents(SDL_AUDIODEVICEADDED, SDL_AUDIODEVICEREMOVED);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(loop, 0, 1);
#else
    while (!done) {
        SDL_Event event;

        while (SDL_PollEvent(&event) > 0) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
            if ((event.type == SDL_AUDIODEVICEADDED && !event.adevice.iscapture) ||
                (event.type == SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture && event.adevice.which == device)) {
                reopen_audio();
            }
        }
        SDL_Delay(100);
    }
#endif

    /* Clean up on signal */
    close_audio();
    SDL_free(wave.sound);
    SDL_Quit();
    return 0;
}
