/*
 * Dreamcast ADPCM streaming sample.
 *
 * This mirrors the PCM looper sample as closely as possible:
 * load the WAV payload, skip the 44-byte header, queue raw bytes into an
 * SDL3 audio stream, and let the Dreamcast backend switch into ADPCM mode
 * via SDL_HINT_AUDIO_ADPCM_STREAM_DC.
 */

#include <SDL3/SDL.h>
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>

#define WAV_PATH "/rd/gs-16b-2c-44100hz_adpcm.wav"

static struct
{
    Uint8 *raw;
    Uint8 *sound;
    Uint32 soundlen;
    Uint32 freq;
    Uint32 channels;
} wave;

static SDL_AudioStream *stream = NULL;

static bool load_dreamcast_adpcm(const char *filename)
{
    Uint8 header[44];
    SDL_IOStream *io = SDL_IOFromFile(filename, "rb");

    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open %s", filename);
        return false;
    }

    if (SDL_ReadIO(io, header, sizeof(header)) != sizeof(header)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't read header from %s", filename);
        SDL_CloseIO(io);
        return false;
    }

    Sint64 filesize = SDL_GetIOSize(io);
    if (filesize < (Sint64)sizeof(header)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s is too small", filename);
        SDL_CloseIO(io);
        return false;
    }

    Sint64 payloadsize = filesize - (Sint64)sizeof(header);
    wave.raw = (Uint8 *)SDL_malloc((size_t)payloadsize);
    if (!wave.raw) {
        SDL_CloseIO(io);
        return false;
    }

    SDL_SeekIO(io, (Sint64)sizeof(header), SDL_IO_SEEK_SET);

    if (SDL_ReadIO(io, wave.raw, (size_t)payloadsize) != (size_t)payloadsize) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't read payload from %s", filename);
        SDL_free(wave.raw);
        wave.raw = NULL;
        SDL_CloseIO(io);
        return false;
    }

    SDL_CloseIO(io);

    /* Parse sample rate and channels from WAV header (little-endian) */
    wave.channels = (Uint32)header[22] | ((Uint32)header[23] << 8);
    wave.freq     = (Uint32)header[24] | ((Uint32)header[25] << 8) |
                    ((Uint32)header[26] << 16) | ((Uint32)header[27] << 24);
    wave.sound    = wave.raw;
    wave.soundlen = (Uint32)payloadsize;

    SDL_Log("Loaded %s: rate=%" SDL_PRIu32 " channels=%" SDL_PRIu32 " len=%" SDL_PRIu32,
            filename, wave.freq, wave.channels, wave.soundlen);
    return true;
}

static void cleanup(void)
{
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = NULL;
    }
    SDL_free(wave.raw);
    wave.raw = NULL;
    wave.sound = NULL;
    wave.soundlen = 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);

    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /*
     * CRITICAL: Set this BEFORE SDL_Init so the DC audio driver
     * knows to enable the raw ADPCM passthrough path and not
     * register a PCM converter for this stream.
     */
    SDL_SetHint(SDL_HINT_AUDIO_ADPCM_STREAM_DC, "1");

    vid_set_mode(DM_640x480, PM_RGB555);

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }

    if (!load_dreamcast_adpcm(WAV_PATH)) {
        cleanup();
        SDL_Quit();
        return 1;
    }

    if (wave.channels == 0 || wave.freq == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid WAV header");
        cleanup();
        SDL_Quit();
        return 1;
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format   = SDL_AUDIO_S8;
    spec.channels = (Uint8)wave.channels;
    spec.freq     = (int)wave.freq;

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);

    if (!stream) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Couldn't open audio stream: %s", SDL_GetError());
        cleanup();
        SDL_Quit();
        return 1;
    }

    SDL_ResumeAudioStreamDevice(stream);
    SDL_Log("Streaming ADPCM: %s", WAV_PATH);

    const int minimum = (int)((wave.soundlen / SDL_AUDIO_FRAMESIZE(spec)) / 2);
    for (;;) {
        if (SDL_GetAudioStreamQueued(stream) < minimum) {
            if (!SDL_PutAudioStreamData(stream, wave.sound, (int)wave.soundlen)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sample put failed: %s", SDL_GetError());
                cleanup();
                SDL_Quit();
                return 1;
            }
        }

        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (!cont) { SDL_Delay(10); continue; }

        cont_state_t *cond = (cont_state_t *)maple_dev_status(cont);
        if (!cond) { SDL_Delay(10); continue; }

        if (cond->buttons & CONT_START) {
            break;
        }

        SDL_Delay(10);
    }

    cleanup();
    SDL_Quit();
    return 0;
}
