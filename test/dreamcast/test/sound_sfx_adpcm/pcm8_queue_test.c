/*
  Dreamcast SDL2 queued-audio PCM8 probe.

  Plays generated 440 Hz tones through SDL_QueueAudio:
    1. AUDIO_U8 with normal unsigned samples
    2. AUDIO_U8 with samples XOR 0x80
    3. AUDIO_S16LSB baseline

  Each set is run as mono and stereo so PCM8 split/counting issues are easy
  to hear on hardware.
*/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "SDL2/SDL.h"

#define TEST_FREQ 22050
#define TEST_SAMPLES 2048
#define TEST_SECONDS 3
#define TEST_TONE_HZ 440.0

static void fill_u8(Uint8 *dst, int frames, int channels, SDL_bool xor_bias)
{
    int i;
    int c;

    for (i = 0; i < frames; i++) {
        const double phase = (2.0 * 3.14159265358979323846 * TEST_TONE_HZ * (double)i) / (double)TEST_FREQ;
        Uint8 sample = (Uint8)(128.0 + (sin(phase) * 96.0));

        if (xor_bias) {
            sample ^= 0x80;
        }

        for (c = 0; c < channels; c++) {
            *dst++ = sample;
        }
    }
}

static void fill_s16(Uint8 *dstbytes, int frames, int channels)
{
    Sint16 *dst = (Sint16 *)dstbytes;
    int i;
    int c;

    for (i = 0; i < frames; i++) {
        const double phase = (2.0 * 3.14159265358979323846 * TEST_TONE_HZ * (double)i) / (double)TEST_FREQ;
        const Sint16 sample = (Sint16)(sin(phase) * 24000.0);

        for (c = 0; c < channels; c++) {
            *dst++ = sample;
        }
    }
}

static const char *format_name(SDL_AudioFormat format)
{
    switch (format) {
        case AUDIO_U8:
            return "AUDIO_U8";
        case AUDIO_S16LSB:
            return "AUDIO_S16LSB";
        default:
            return "unknown";
    }
}

static int play_case(const char *name, SDL_AudioFormat format, int channels, SDL_bool xor_bias)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_AudioDeviceID dev;
    Uint8 *audio;
    int frames = TEST_FREQ * TEST_SECONDS;
    int bytes_per_sample = SDL_AUDIO_BITSIZE(format) / 8;
    int len = frames * channels * bytes_per_sample;
    int i;

    SDL_zero(want);
    want.freq = TEST_FREQ;
    want.format = format;
    want.channels = (Uint8)channels;
    want.samples = TEST_SAMPLES;
    want.callback = NULL;

    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        printf("SDL_OpenAudioDevice failed for %s: %s\n", name, SDL_GetError());
        return -1;
    }

    printf("\n[%s]\n", name);
    printf("  expected: %s, %s, 440 Hz sine, %s samples\n",
           format_name(format),
           channels == 1 ? "mono" : "stereo",
           xor_bias ? "0x80-biased U8 (high bit flipped)" : "native unsigned U8");
    printf("  listen for: %s\n",
           (format == AUDIO_S16LSB) ? "clean baseline tone" :
           (xor_bias ? "an intentionally wrong/inverted PCM8 tone after the backend fix" :
                       "the native PCM8 path under test, which should now match the S16 baseline closely"));
    printf("  requested fmt=0x%04x ch=%d, got fmt=0x%04x ch=%d freq=%d samples=%d size=%lu silence=0x%02x\n",
           want.format, channels, have.format, have.channels, have.freq,
           have.samples, (unsigned long)have.size, have.silence);
    printf("  queued bytes: %d\n", len);
    printf("  note: SDL_U8 silence should be 0x80\n");

    audio = (Uint8 *)SDL_malloc(len);
    if (!audio) {
        SDL_CloseAudioDevice(dev);
        return -1;
    }

    if (format == AUDIO_U8) {
        fill_u8(audio, frames, channels, xor_bias);
    } else {
        fill_s16(audio, frames, channels);
    }

    printf("first bytes:");
    for (i = 0; i < 16 && i < len; i++) {
        printf(" %02x", audio[i]);
    }
    printf("\n");

    if (SDL_QueueAudio(dev, audio, (Uint32)len) < 0) {
        printf("SDL_QueueAudio failed for %s: %s\n", name, SDL_GetError());
        SDL_free(audio);
        SDL_CloseAudioDevice(dev);
        return -1;
    }

    SDL_PauseAudioDevice(dev, 0);
    printf("  playback started\n");
    while (SDL_GetQueuedAudioSize(dev) > 0) {
        SDL_Delay(50);
    }
    SDL_Delay(350);
    printf("  playback drained\n");

    SDL_CloseAudioDevice(dev);
    SDL_free(audio);
    SDL_Delay(700);
    printf("  device closed\n");
    return 0;
}

int main(int argc, char **argv)
{
    int channels;
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    for (channels = 1; channels <= 2; channels++) {
        char label[64];

        printf("\n=== %s ===\n", channels == 1 ? "mono tests" : "stereo tests");

        SDL_snprintf(label, sizeof(label), "AUDIO_U8 unsigned %s", channels == 1 ? "mono" : "stereo");
        play_case(label, AUDIO_U8, channels, SDL_FALSE);

        SDL_snprintf(label, sizeof(label), "AUDIO_U8 xor80 %s", channels == 1 ? "mono" : "stereo");
        play_case(label, AUDIO_U8, channels, SDL_TRUE);

        SDL_snprintf(label, sizeof(label), "AUDIO_S16LSB %s", channels == 1 ? "mono" : "stereo");
        play_case(label, AUDIO_S16LSB, channels, SDL_FALSE);
    }

    SDL_Quit();
    printf("PCM8 queue test complete\n");
    return 0;
}
