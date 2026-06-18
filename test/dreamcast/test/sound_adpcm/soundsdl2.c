/*
 * Dreamcast ADPCM streaming sample.
 *
 * Streams raw ADPCM bytes through the SDL audio callback path using a
 * main-thread ring buffer to avoid VFS I/O inside the AICA callback.
 * SDL_HINT_AUDIO_ADPCM_STREAM_DC makes the Dreamcast audio driver start
 * KOS in ADPCM streaming mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL2/SDL.h"

#ifdef DREAMCAST
#include "kos.h"
#define WAV_PATH "/rd/gs-16b-2c-44100hz_adpcm.wav"
extern uint8 romdisk[];
KOS_INIT_ROMDISK(romdisk);
#else
#define WAV_PATH "data/gs-16b-2c-44100hz.wav"
#endif

/* Ring buffer size — 256 KB gives plenty of headroom for KOS VFS latency. */
#define STREAM_RING_SIZE (256 * 1024)

/* How many bytes to read from disk per main-loop tick. */
#define STREAM_CHUNK_SIZE 4096

static struct {
    SDL_AudioSpec spec;
    SDL_RWops    *rw;
    Uint32        soundlen;    /* byte length of the audio payload          */
    Sint64        data_start;  /* file offset of the first payload byte     */

    /* Ring buffer — written by main thread, read by audio callback.
     * ring_read/ring_write are plain Uint32 counters that wrap naturally;
     * the actual index into ring[] is (counter % STREAM_RING_SIZE).
     * Only the main thread writes ring_write; only the callback reads it.
     * Only the callback writes ring_read; only the main thread reads it.
     * No mutex needed for this single-producer / single-consumer pattern. */
    Uint8            ring[STREAM_RING_SIZE];
    volatile Uint32  ring_read;
    volatile Uint32  ring_write;
    int              eof;       /* non-zero once we have reached end-of-file */
} wave;

static SDL_AudioDeviceID device;

/* -------------------------------------------------------------------------- */

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

static void close_wave_stream(void) {
    if (wave.rw != NULL) {
        SDL_RWclose(wave.rw);
        wave.rw = NULL;
    }
}

/* --------------------------------------------------------------------------
 * WAV parser — walks RIFF chunks so it handles files with metadata chunks
 * (LIST, INFO, etc.) that precede or follow the data chunk.
 * Returns 0 on success, -1 on error (SDL error string set).
 * On success *stream is positioned at the first payload byte.
 * -------------------------------------------------------------------------- */
static Uint16 read_u16_le(const Uint8 *p) {
    return (Uint16)((Uint16)p[0] | ((Uint16)p[1] << 8));
}
static Uint32 read_u32_le(const Uint8 *p) {
    return (Uint32)p[0] | ((Uint32)p[1] << 8) |
           ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

static int load_wav_stream(const char *filename,
                           SDL_AudioSpec *spec,
                           SDL_RWops    **out_rw,
                           Uint32        *out_len,
                           Sint64        *out_data_start)
{
    SDL_RWops *rw;
    Uint8  riff[12], ch[8], fmt[16];
    Uint16 audio_format = 0, channels = 0, bits = 0;
    Uint32 sample_rate = 0, data_size = 0;
    Sint64 payload_start = -1;
    int have_fmt = 0, have_data = 0;

    rw = SDL_RWFromFile(filename, "rb");
    if (!rw) return -1;

    if (SDL_RWread(rw, riff, 1, sizeof(riff)) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 ||
        memcmp(riff + 8, "WAVE", 4) != 0) {
        SDL_SetError("Not a WAV file: %s", filename);
        SDL_RWclose(rw);
        return -1;
    }

    while (!(have_fmt && have_data)) {
        Uint32 chunk_size;
        Sint64 chunk_data_start;

        if (SDL_RWread(rw, ch, 1, sizeof(ch)) != sizeof(ch)) {
            SDL_SetError("Truncated WAV: %s", filename);
            SDL_RWclose(rw);
            return -1;
        }
        chunk_size       = read_u32_le(ch + 4);
        chunk_data_start = SDL_RWtell(rw);

        if (memcmp(ch, "fmt ", 4) == 0) {
            if (chunk_size < sizeof(fmt)) {
                SDL_SetError("fmt chunk too small");
                SDL_RWclose(rw);
                return -1;
            }
            if (SDL_RWread(rw, fmt, 1, sizeof(fmt)) != sizeof(fmt)) {
                SDL_SetError("Failed to read fmt chunk");
                SDL_RWclose(rw);
                return -1;
            }
            audio_format = read_u16_le(fmt + 0);
            channels     = read_u16_le(fmt + 2);
            sample_rate  = read_u32_le(fmt + 4);
            bits         = read_u16_le(fmt + 14);
            if (chunk_size > sizeof(fmt)) {
                SDL_RWseek(rw, (Sint64)(chunk_size - sizeof(fmt)), RW_SEEK_CUR);
            }
            have_fmt = 1;
        } else if (memcmp(ch, "data", 4) == 0) {
            payload_start = chunk_data_start;
            data_size     = chunk_size;
            have_data     = 1;
            /* Don't seek past the data chunk — we stop here and leave the
             * file pointer at the start of the payload. */
        } else {
            SDL_RWseek(rw, (Sint64)chunk_size, RW_SEEK_CUR);
        }

        /* RIFF chunks are word-aligned. */
        if (chunk_size & 1u) {
            SDL_RWseek(rw, 1, RW_SEEK_CUR);
        }
    }

    if (payload_start < 0) {
        SDL_SetError("No data chunk in WAV: %s", filename);
        SDL_RWclose(rw);
        return -1;
    }

    /* Seek to the first payload byte. */
    if (SDL_RWseek(rw, payload_start, RW_SEEK_SET) < 0) {
        SDL_SetError("Failed to seek to WAV payload");
        SDL_RWclose(rw);
        return -1;
    }

    SDL_zero(*spec);
    spec->freq     = (int)sample_rate;
    spec->channels = (Uint8)channels;

#ifdef DREAMCAST
    (void)audio_format;
    (void)bits;
    /* Dreamcast driver bypasses SDL format handling when the ADPCM hint is
     * set — the raw bytes from the WAV payload are fed directly to KOS.
     * Present them as S16 so SDL's buffer-size math stays sensible, but the
     * actual data flowing through the callback is packed 4-bit ADPCM. */
    spec->format  = AUDIO_S16;
    spec->samples = 2048;
    spec->silence = 0x00;
#else
    if (audio_format == 1 && bits == 8) {
        spec->format  = AUDIO_U8;
        spec->silence = 0x80;
    } else if (audio_format == 1 && bits == 16) {
        spec->format  = AUDIO_S16SYS;
        spec->silence = 0x00;
    } else {
        SDL_SetError("Unsupported WAV encoding: format=%u bits=%u",
                     audio_format, bits);
        SDL_RWclose(rw);
        return -1;
    }
    spec->samples = 4096;
#endif

    spec->size = data_size;

    *out_rw         = rw;
    *out_len        = data_size;
    *out_data_start = payload_start;
    return 0;
}

/* --------------------------------------------------------------------------
 * Ring-buffer helpers (main thread only — no locking needed for SPSC).
 * -------------------------------------------------------------------------- */

/* Returns how many bytes of free space are available for writing. */
static SDL_INLINE Uint32 ring_free(void) {
    return STREAM_RING_SIZE - (wave.ring_write - wave.ring_read);
}

/* Feed up to STREAM_CHUNK_SIZE bytes from disk into the ring each call.
 * Loops back to the start of the audio payload when EOF is reached. */
static void ring_pump(void) {
    while (ring_free() >= STREAM_CHUNK_SIZE) {
        Uint32 wpos  = wave.ring_write % STREAM_RING_SIZE;
        /* Don't write past the end of the physical ring array. */
        Uint32 chunk = SDL_min(STREAM_CHUNK_SIZE, STREAM_RING_SIZE - wpos);
        size_t got   = SDL_RWread(wave.rw, wave.ring + wpos, 1, chunk);
        if (got == 0) {
            /* EOF — loop back to the start of the audio payload. */
            if (SDL_RWseek(wave.rw, wave.data_start, RW_SEEK_SET) < 0) {
                wave.eof = 1;
                return;
            }
            continue;
        }
        /* Barrier: ensure data is visible to the callback before we
         * advance ring_write.  SDL_MemoryBarrierRelease is a no-op on
         * single-core SH-4 but documents the intent clearly. */
        SDL_MemoryBarrierRelease();
        wave.ring_write += (Uint32)got;
    }
}

/* --------------------------------------------------------------------------
 * SDL audio callback — runs in the audio thread (KOS AICA callback context).
 * Must NOT call any VFS / file I/O functions.
 * -------------------------------------------------------------------------- */
void SDLCALL fillerup(void *userdata, Uint8 *stream, int len) {
    (void)userdata;

    while (len > 0) {
        Uint32 avail = wave.ring_write - wave.ring_read;

        if (avail == 0) {
            /* Ring is empty — output silence and bail. */
            SDL_memset(stream, wave.spec.silence, len);
            return;
        }

        Uint32 rpos  = wave.ring_read % STREAM_RING_SIZE;
        Uint32 chunk = SDL_min((Uint32)len,
                               SDL_min(avail, STREAM_RING_SIZE - rpos));

        SDL_memcpy(stream, wave.ring + rpos, chunk);

        SDL_MemoryBarrierRelease();
        wave.ring_read += chunk;

        stream += chunk;
        len    -= (int)chunk;
    }
}

/* -------------------------------------------------------------------------- */

static void open_audio(void) {
    SDL_Log("Opening audio device: freq=%d fmt=%d ch=%d samples=%d",
            wave.spec.freq, wave.spec.format,
            wave.spec.channels, wave.spec.samples);

    device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &wave.spec, NULL, 0);
    if (!device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Couldn't open audio: %s\n", SDL_GetError());
        close_wave_stream();
        quit(2);
    }

    SDL_PauseAudioDevice(device, 0);
    if (SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_PLAYING) {
        SDL_Log("Audio device did not start playing: %s", SDL_GetError());
    }
    SDL_Log("Audio device opened successfully (id=%u)", device);
}

static void reopen_audio(void) {
    close_audio();
    open_audio();
}

/* -------------------------------------------------------------------------- */

static int done = 0;

#ifdef __EMSCRIPTEN__
void loop(void) {
    if (done || SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_PLAYING) {
        emscripten_cancel_main_loop();
    }
}
#endif

int main(int argc, char *argv[]) {
    const char *filename = WAV_PATH;
    SDL_Window   *window;
    SDL_Renderer *renderer;
    (void)argc;
    (void)argv;

#ifdef DREAMCAST
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);
#endif

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /* Set ADPCM hint BEFORE SDL_Init so the audio driver sees it at
     * bootstrap time (snd_stream_init path). */
    SDL_SetHint("SDL_AUDIO_ADPCM_STREAM_DC", "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("ADPCM Streaming",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              640, 480,
                              SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Window error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Renderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Log("Loading %s", filename);

    SDL_memset(&wave, 0, sizeof(wave));

    if (load_wav_stream(filename, &wave.spec,
                        &wave.rw, &wave.soundlen, &wave.data_start) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Couldn't open %s: %s\n", filename, SDL_GetError());
        quit(1);
    }

    /* Pre-fill the ring buffer before opening the device so the callback
     * never starves on the very first invocation. */
    while (ring_free() >= STREAM_CHUNK_SIZE) {
        ring_pump();
    }

    wave.spec.callback = fillerup;

    SDL_Log("Available audio drivers:");
    for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i) {
        SDL_Log("  %d: %s", i, SDL_GetAudioDriver(i));
    }
    SDL_Log("Using audio driver: %s", SDL_GetCurrentAudioDriver());

    open_audio();

    SDL_FlushEvents(SDL_AUDIODEVICEADDED, SDL_AUDIODEVICEREMOVED);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(loop, 0, 1);
#else
    while (!done) {
        SDL_Event event;

        /* Keep the ring buffer fed from the main thread. */
        ring_pump();

        while (SDL_PollEvent(&event) > 0) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
            if ((event.type == SDL_AUDIODEVICEADDED && !event.adevice.iscapture) ||
                (event.type == SDL_AUDIODEVICEREMOVED &&
                 !event.adevice.iscapture &&
                 event.adevice.which == device)) {
                reopen_audio();
            }
        }

        SDL_Delay(10);  /* 10 ms tick — plenty of time to keep ring full */
    }
#endif

    close_audio();
    close_wave_stream();
    SDL_Quit();
    return 0;
}