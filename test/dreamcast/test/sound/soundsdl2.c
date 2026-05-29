
#include <stdio.h>
#include <stdlib.h>
#include "SDL2/SDL.h"

#ifdef DREAMCAST
#include "kos.h"
// #include "SDL_hints.h"
#define WAV_PATH "/rd/gs-16b-2c-44100hz.wav"
extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);
#else
#define WAV_PATH "data/sample.wav"
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
    char *filename = WAV_PATH;
    SDL_Window *window;
    SDL_Renderer *renderer;

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
    /* Load the wave file into memory */
    if (SDL_LoadWAV(filename, &wave.spec, &wave.sound, &wave.soundlen) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s\n", filename, SDL_GetError());
        quit(1);
    }

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
    SDL_FreeWAV(wave.sound);
    SDL_Quit();
    return 0;
}
