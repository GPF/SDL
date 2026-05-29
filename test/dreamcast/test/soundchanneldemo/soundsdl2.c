/* KallistiOS ##version##

   sdl/sound/sdl_sound.c
   Copyright (C) 2016 Lawrence Sebald
*/
#include <stdio.h>
#include <stdlib.h>
#include "SDL2/SDL.h"
#include <dc/fmath.h>

/* Amplitude controls the volume of the generated sound. */
#define AMPLITUDE 16384

/* 44100HZ audio frequency... Pretty standard. */
#define FRQ_44KHZ 44100

/* Increment for sine wave calculation */
#define INCREMENT 1.0f / 100.0f

/* Sides for audio output */
#define LEFT 0
#define RIGHT 1
#define CENTER 2

static float pos = 0.0f;
static int current_side = LEFT;
static int samples_per_channel = 0;
static int switch_interval = FRQ_44KHZ ;  /* Switch every half-second */

/* Generate a sine wave with output to a specific speaker. */
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    Sint16 *out = (Sint16 *)stream;
    int i;

    /* Length is in bytes, we're generating 16-bit samples (2 bytes), and stereo, so divide length by 4 */
    len >>= 2;

    for(i = 0; i < len; ++i) {
        if(current_side == LEFT) {
            out[2 * i] = (Sint16)(fsin(pos * F_PI) * AMPLITUDE);  /* Left channel */
            out[2 * i + 1] = 0;                                  /* Right channel */
        }
        else if(current_side == RIGHT) {
            out[2 * i] = 0;                                      /* Left channel */
            out[2 * i + 1] = (Sint16)(fsin(pos * F_PI) * AMPLITUDE);  /* Right channel */
        }
        else if(current_side == CENTER) {
            out[2 * i] = (Sint16)(fsin(pos * F_PI) * AMPLITUDE);  /* Left channel */
            out[2 * i + 1] = (Sint16)(fsin(pos * F_PI) * AMPLITUDE);  /* Right channel */
        }

        pos += INCREMENT;

        if(pos >= 2.0f)
            pos -= 2.0f;
    }

    /* Switch the output side after processing a certain number of samples */
    samples_per_channel += len;
    if(samples_per_channel >= switch_interval) {  
        samples_per_channel = 0;
        if(current_side == LEFT) {
            current_side = RIGHT;
            SDL_Log("Playing on: RIGHT speaker");
        }
        else if(current_side == RIGHT) {
            current_side = CENTER;
            SDL_Log("Playing on: CENTER (BOTH) speakers");
        }
        else if(current_side == CENTER) {
            current_side = LEFT;
            SDL_Log("Playing on: LEFT speaker");
        }
    }
}

int main(int argc, char *argv[]) {
    SDL_AudioSpec desired_spec, obtained_spec;
    SDL_AudioDeviceID audio_device;
    int done = 0;

    /* Initialize SDL2 audio system */
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    /* Set up the desired audio format */
    SDL_zero(desired_spec);
    desired_spec.freq = FRQ_44KHZ;
    desired_spec.format = AUDIO_S16LSB;
    desired_spec.channels = 2;  /* Stereo output */
    desired_spec.samples = 2048;
    desired_spec.callback = audio_callback;

    /* Open the audio device */
    audio_device = SDL_OpenAudioDevice(NULL, 0, &desired_spec, &obtained_spec, 0);
    if (!audio_device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open audio: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Log("Audio device opened successfully with the following spec:");
    SDL_Log("  Frequency: %d", obtained_spec.freq);
    SDL_Log("  Format: %d", obtained_spec.format);
    SDL_Log("  Channels: %d", obtained_spec.channels);
    SDL_Log("  Samples: %d", obtained_spec.samples);


    /* Start playing the audio */
    SDL_PauseAudioDevice(audio_device, 0);
    SDL_Log("Playing on: LEFT speaker");
    /* Let the sound play for 12 seconds (to ensure each side gets time) */
    SDL_Delay(24 * 1000);

    /* Clean up and exit */
    SDL_PauseAudioDevice(audio_device, 1);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();

    return 0;
}
