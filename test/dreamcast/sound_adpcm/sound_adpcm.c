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
    Uint32 soundpos;
    Uint32 freq;
    Uint32 channels;
} wave;

static SDL_AudioStream *stream = NULL;

static bool load_dreamcast_adpcm(const char *filename)
{
    SDL_IOStream *io = SDL_IOFromFile(filename, "rb");
    SDL_AudioSpec spec;

    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open %s", filename);
        return false;
    }

    if (!SDL_LoadDreamcastADPCM_IO(io, true, &spec, &wave.raw, &wave.soundlen)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", filename, SDL_GetError());
        return false;
    }

    wave.sound    = wave.raw;
    wave.freq     = (Uint32)spec.freq;
    wave.channels = (Uint32)spec.channels;

    SDL_Log("Loaded %s: rate=%" SDL_PRIu32 " channels=%" SDL_PRIu32 " len=%" SDL_PRIu32,
            filename, wave.freq, wave.channels, wave.soundlen);
    return true;
}

static void SDLCALL fill_stream(void *userdata, SDL_AudioStream *audio_stream, int additional_amount, int total_amount)
{
    (void)userdata;
    (void)total_amount;

    while (additional_amount > 0 && wave.sound && wave.soundlen > 0) {
        const Uint32 waveleft = wave.soundlen - wave.soundpos;
        const int chunk = (waveleft < (Uint32)additional_amount) ? (int)waveleft : additional_amount;

        if (!SDL_PutAudioStreamData(audio_stream, wave.sound + wave.soundpos, chunk)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sample put failed: %s", SDL_GetError());
            return;
        }

        additional_amount -= chunk;
        wave.soundpos += (Uint32)chunk;

        if (wave.soundpos >= wave.soundlen) {
            wave.soundpos = 0;
        }
    }
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
    spec.format   = SDL_AUDIO_S16LE;
    spec.channels = (Uint8)wave.channels;
    spec.freq     = (int)wave.freq;

    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, fill_stream, NULL);

    if (!stream) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Couldn't open audio stream: %s", SDL_GetError());
        cleanup();
        SDL_Quit();
        return 1;
    }

    SDL_ResumeAudioStreamDevice(stream);
    SDL_Log("Streaming ADPCM: %s", WAV_PATH);

    for (;;) {
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
