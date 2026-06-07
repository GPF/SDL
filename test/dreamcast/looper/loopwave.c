/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* Program to load a wave file and loop playing it using SDL audio */
#include <stdlib.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_test.h>
#include "testutils.h"

#ifdef SDL_PLATFORM_DREAMCAST

#define WAV_PATH "/rd/gs-16b-2c-44100hz.wav"

static struct
{
    SDL_AudioSpec spec;
    Uint8 *sound;
    Uint32 soundlen;
    Uint32 soundpos;
} wave;

static SDL_AudioStream *stream;
static SDLTest_CommonState *state;

static int fillerup(void)
{
    const int minimum = (wave.soundlen / SDL_AUDIO_FRAMESIZE(wave.spec)) / 2;
    if (SDL_GetAudioStreamQueued(stream) < minimum) {
        SDL_PutAudioStreamData(stream, wave.sound, (int) wave.soundlen);
    }
    return SDL_APP_CONTINUE;
}

static void cleanup(void)
{
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }

    SDL_free(wave.sound);
    wave.sound = NULL;
    wave.soundlen = 0;
    wave.soundpos = 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    int i;
    (void) appstate;
    (void) argc;
    (void) argv;
  
    /* this doesn't have to run very much, so give up tons of CPU time between iterations. */
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "5");

    /* Initialize test framework */
    state = SDLTest_CommonCreateState(argv, 0);
    if (!state) {
        return SDL_APP_SUCCESS;
    }

    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_LoadWAV(WAV_PATH, &wave.spec, &wave.sound, &wave.soundlen)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", WAV_PATH, SDL_GetError());
        cleanup();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    SDL_Log("Loaded %s: rate=%d channels=%d len=%" SDL_PRIu32,
            WAV_PATH, wave.spec.freq, wave.spec.channels, wave.soundlen);

    /* Show the list of available drivers */
    SDL_Log("Available audio drivers:");
    for (i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
        SDL_Log("%i: %s", i, SDL_GetAudioDriver(i));
    }

    SDL_Log("Using audio driver: %s", SDL_GetCurrentAudioDriver());
    SDL_Log("Current audio device name: %s", SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK));

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wave.spec, NULL, NULL);
    if (!stream) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create audio stream: %s", SDL_GetError());
        cleanup();
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    SDL_ResumeAudioStreamDevice(stream);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    (void) appstate;
    return (event->type == SDL_EVENT_QUIT) ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    (void) appstate;
    return fillerup();
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void) appstate;
    (void) result;
    cleanup();
    SDL_Quit();
    SDLTest_CommonDestroyState(state);
}

#endif /* SDL_PLATFORM_DREAMCAST */
