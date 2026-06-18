/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software in a product,
     an acknowledgment in the product documentation would be appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_DREAMCAST

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "SDL_dreamcastaudio.h"  /* Must define SDL_PrivateAudioData as above */
#include <dc/sound/stream.h>
#include <dc/sound/sound.h>
#include <kos/thread.h>
#include "../SDL_sysaudio.h"
#include "SDL_timer.h"
#include "SDL_hints.h"
#include "kos.h"
#include <dc/sound/sfxmgr.h>

#define DREAMCASTAUD_MAX_ADPCM_SFX 64

typedef struct
{
    const Uint8 *buf;
    Uint32 len;
    sfxhnd_t handle;
} DreamcastADPCMSfx;

/* Global device references */
static SDL_AudioDevice *audioDevice = NULL;   /* Active output device */
static SDL_AudioDevice *captureDevice = NULL;   /* Active capture device */
static DreamcastADPCMSfx adpcm_sfx[DREAMCASTAUD_MAX_ADPCM_SFX];

int SDL_DreamcastRegisterADPCMSfx(const Uint8 *buf, Uint32 len, int rate, int channels)
{
    int free_slot = -1;
    sfxhnd_t handle;
    int i;

    if (!buf || len == 0) {
        return SDL_InvalidParamError("buf");
    }

    for (i = 0; i < SDL_arraysize(adpcm_sfx); i++) {
        if (adpcm_sfx[i].buf == buf) {
            return 0;
        }
        if (!adpcm_sfx[i].buf && free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        return SDL_SetError("No free Dreamcast ADPCM SFX slots");
    }

    if (snd_init() < 0) {
        return SDL_SetError("snd_init failed");
    }

    handle = snd_sfx_load_raw_buf((char *)buf, len, (uint32_t)rate, 4, (uint16_t)channels);
    if (handle == SFXHND_INVALID) {
        return SDL_SetError("snd_sfx_load_raw_buf failed");
    }

    adpcm_sfx[free_slot].buf = buf;
    adpcm_sfx[free_slot].len = len;
    adpcm_sfx[free_slot].handle = handle;
    return 0;
}

int SDL_DreamcastQueueADPCMSfx(const void *data, Uint32 len)
{
    int i;

    for (i = 0; i < SDL_arraysize(adpcm_sfx); i++) {
        if (adpcm_sfx[i].buf == data) {
            if (adpcm_sfx[i].len != len) {
                return SDL_SetError("Dreamcast ADPCM SFX length mismatch");
            }
            return (snd_sfx_play(adpcm_sfx[i].handle, 255, 128) >= 0) ? 1 : SDL_SetError("snd_sfx_play failed");
        }
    }

    return 0;
}

void SDL_DreamcastUnregisterADPCMSfx(const Uint8 *buf)
{
    int i;

    if (!buf) {
        return;
    }

    for (i = 0; i < SDL_arraysize(adpcm_sfx); i++) {
        if (adpcm_sfx[i].buf == buf) {
            snd_sfx_unload(adpcm_sfx[i].handle);
            SDL_zero(adpcm_sfx[i]);
            return;
        }
    }
}

/*
 * Return pointer to the current active buffer.
 * The SDL mixing thread writes raw 4-bit ADPCM data here.
 */
static Uint8 *DREAMCASTAUD_GetDeviceBuf(_THIS)
{
    SDL_PrivateAudioData *hidden = (SDL_PrivateAudioData *)_this->hidden;
    /* Use SDL_AtomicGet to retrieve the current active buffer index */
    return hidden->mixbuf[ SDL_AtomicGet(&hidden->active_buffer) ];
}

static void DREAMCASTAUD_ConvertPCM8ToSigned(Uint8 *buffer, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        buffer[i] ^= 0x80;
    }
}

/*
 * Stream callback invoked by the KOS sound stream system.
 * We lock the mutex to safely access our atomic flags.
 */
static void *stream_callback(snd_stream_hnd_t hnd, int req, int *done) {
    SDL_AudioDevice *device = audioDevice;
    SDL_PrivateAudioData *hidden = NULL;
    const int requested = req;
    *done = 0;

    if (!device || !device->hidden) {
        return NULL;
    }

    hidden = (SDL_PrivateAudioData *)device->hidden;

    if (SDL_AtomicGet(&hidden->buffer_ready)) {
        /* Get the next buffer index */
        const int current_active = SDL_AtomicGet(&hidden->active_buffer);
        const int next_buf = current_active ^ 1;
        const int buffer_size = hidden->buffer_size;

        *done = SDL_min(req, buffer_size);

        if (device->spec.format == AUDIO_U8) {
            DREAMCASTAUD_ConvertPCM8ToSigned(hidden->mixbuf[current_active], *done);
        }

        SDL_AtomicSet(&hidden->active_buffer, next_buf);
        SDL_AtomicSet(&hidden->buffer_ready, 0);

        // SDL_Log("Switching to buffer %d (%d bytes)", next_buf, *done);
        return hidden->mixbuf[current_active];
    }

    if (hidden->silencebuf) {
        *done = SDL_min(requested, hidden->buffer_size);
        return hidden->silencebuf;
    }
    return NULL;
}
/*
 * WaitDevice - block until the current buffer has been consumed.
 */
static void DREAMCASTAUD_WaitDevice(_THIS)
{
    SDL_PrivateAudioData *hidden = (SDL_PrivateAudioData *)_this->hidden;
    // SDL_Log("WAITDEVICE");

    // if (SDL_AtomicGet(&_this->paused)) return;
    if (!hidden || hidden->stream_handle == SND_STREAM_INVALID) {
        return;
    }

    /* Exit immediately once the core has started teardown. */
    while (SDL_AtomicGet(&hidden->buffer_ready)) {
        if (SDL_AtomicGet(&_this->shutdown) ||
            SDL_AtomicGet(&_this->paused) ||
            !SDL_AtomicGet(&_this->enabled)) {
            SDL_AtomicSet(&hidden->buffer_ready, 0);
            break;
        }

        snd_stream_poll(hidden->stream_handle);
        SDL_Delay(1);
    }
}

/*
 * PlayDevice - mark the current mix buffer as ready.
 * We assume that the SDL mixing thread writes raw 4-bit ADPCM data
 * into the active mix buffer (via SDL_GetDeviceBuf), so here we only need
 * to flag that the buffer is ready and nudge the KOS stream.
 */
static void DREAMCASTAUD_PlayDevice(_THIS)
{
    SDL_PrivateAudioData *hidden = (SDL_PrivateAudioData *)_this->hidden;
    // SDL_Log("PLAYDEVICE");
    // if (!SDL_AtomicGet(&_this->enabled)) return;
    // if (SDL_AtomicGet(&_this->paused)) return;
    if (!hidden || hidden->stream_handle == SND_STREAM_INVALID) {
        return;
    }

    if (SDL_AtomicGet(&_this->shutdown) ||
        SDL_AtomicGet(&_this->paused) ||
        !SDL_AtomicGet(&_this->enabled)) {
        SDL_AtomicSet(&hidden->buffer_ready, 0);
        return;
    }

    /* Wait until the previous buffer has been consumed */
    DREAMCASTAUD_WaitDevice(_this);

    // SDL_LockMutex(hidden->lock);
    if (SDL_AtomicGet(&_this->shutdown) ||
        SDL_AtomicGet(&_this->paused) ||
        !SDL_AtomicGet(&_this->enabled)) {
        SDL_AtomicSet(&hidden->buffer_ready, 0);
        return;
    }
    SDL_AtomicSet(&hidden->buffer_ready, 1);
    // SDL_UnlockMutex(hidden->lock);

    /* Nudge the stream so that the callback is invoked */
    // snd_stream_poll(hidden->stream_handle);

    // SDL_Log("Committed buffer %d", SDL_AtomicGet(&hidden->active_buffer));
}

/*
 * Thread initialization: raise thread priority.
 */
static void DREAMCASTAUD_ThreadInit(_THIS)
{
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
}

static void DREAMCASTAUD_ThreadDeinit(_THIS)
{
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
}

SDL_AudioSpec *SDL_LoadDreamcastADPCM_RW(SDL_RWops *src, int freesrc, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len)
{
    Uint8 header[44];
    Uint32 sampleRate;
    Uint16 channels;

    if (!src) {
        return NULL;
    } else if (!spec) {
        SDL_InvalidParamError("spec");
        return NULL;
    } else if (!audio_buf) {
        SDL_InvalidParamError("audio_buf");
        return NULL;
    } else if (!audio_len) {
        SDL_InvalidParamError("audio_len");
        return NULL;
    }

    if (SDL_RWread(src, header, sizeof(header), 1) != 1) {
        SDL_SetError("Failed to read ADPCM header");
        goto fail;
    }

    sampleRate = (Uint32)header[24] | ((Uint32)header[25] << 8) | ((Uint32)header[26] << 16) | ((Uint32)header[27] << 24);
    channels = (Uint16)header[22];

    if (SDL_RWseek(src, 44, RW_SEEK_SET) < 0) {
        SDL_SetError("Failed to seek to ADPCM data");
        goto fail;
    }

    *audio_len = (Uint32)(SDL_RWsize(src) - 44);
    *audio_buf = (Uint8 *)SDL_malloc(*audio_len);
    if (!*audio_buf) {
        SDL_OutOfMemory();
        goto fail;
    }

    if (SDL_RWread(src, *audio_buf, *audio_len, 1) != 1) {
        SDL_free(*audio_buf);
        *audio_buf = NULL;
        SDL_SetError("Failed to read ADPCM data");
        goto fail;
    }

    SDL_zero(*spec);
    spec->freq = (int)sampleRate;
    spec->format = AUDIO_S8;
    spec->channels = (Uint8)channels;
    spec->samples = 512;
    spec->size = *audio_len;

    if (SDL_DreamcastRegisterADPCMSfx(*audio_buf, *audio_len, spec->freq, spec->channels) < 0) {
        SDL_free(*audio_buf);
        *audio_buf = NULL;
        goto fail;
    }

    SDL_Log("ADPCM file loaded successfully: %" SDL_PRIu32 " bytes", *audio_len);

    if (freesrc) {
        SDL_RWclose(src);
    }

    return spec;

fail:
    if (audio_buf) {
        *audio_buf = NULL;
    }
    if (audio_len) {
        *audio_len = 0;
    }
    if (freesrc) {
        SDL_RWclose(src);
    }
    return NULL;
}

/*
 * Open the audio device.
 * This function initializes the KOS sound stream, allocates double-buffering,
 * and sets up the stream callback.
 */
int DREAMCASTAUD_OpenDevice(_THIS, const char *devname)
{
    SDL_PrivateAudioData *hidden;
    SDL_AudioFormat test_format;
    int bytes_per_sample;
    int channels, frequency;
    const char *adpcm_hint;  /* ADPCM hint from SDL hints */
    SDL_bool adpcm_stream = SDL_FALSE;

    SDL_Log("Opening audio device\n");
        /* Initialize the sound stream system */
    if (snd_stream_init() != 0) {
        return SDL_SetError("Failed to initialize sound stream system");
    }
    hidden = (SDL_PrivateAudioData *)SDL_malloc(sizeof(*hidden));
    if (!hidden) {
        return SDL_OutOfMemory();
    }
    SDL_zerop(hidden);
    hidden->stream_handle = SND_STREAM_INVALID;
    _this->hidden = (struct SDL_PrivateAudioData *)hidden;

    adpcm_hint = SDL_GetHint("SDL_AUDIO_ADPCM_STREAM_DC");
    adpcm_stream = (adpcm_hint && SDL_strcmp(adpcm_hint, "1") == 0) ? SDL_TRUE : SDL_FALSE;
    if (adpcm_stream) {
        _this->spec.format = AUDIO_S8;
    }

    /* Ensure that the shutdown flag is clear for the new session */
    // SDL_AtomicSet(&_this->shutdown, 0);
    /* Initialize the sound stream system */
    // if (snd_stream_init() != 0) {
    //     SDL_free(hidden);
    //     return SDL_SetError("Failed to initialize sound stream system");
    // }

    /* Choose a compatible audio format */
    for (test_format = SDL_FirstAudioFormat(_this->spec.format);
         test_format;
         test_format = SDL_NextAudioFormat()) {
        if ((adpcm_stream && test_format == AUDIO_S8) ||
            (!adpcm_stream && ((test_format == AUDIO_U8) || (test_format == AUDIO_S16LSB)))) {
            _this->spec.format = test_format;
            break;
        }
    }
    if (!test_format) {
        snd_stream_shutdown();
        SDL_free(hidden);
        return SDL_SetError("Dreamcast unsupported audio format: 0x%x", _this->spec.format);
    }

    SDL_CalculateAudioSpec(&_this->spec);
    bytes_per_sample = SDL_AUDIO_BITSIZE(_this->spec.format) / 8;
    if (adpcm_stream) {
        /* ADPCM bytes are already packed; expose them to SDL as 8-bit sized
         * buffers so the callback stays byte-oriented. */
        hidden->buffer_size = _this->spec.samples * _this->spec.channels;
    } else {
        /* KOS streams ask for half of the circular buffer per channel. Size
         * the KOS buffer so each request matches one SDL-filled device buffer. */
        hidden->buffer_size = _this->spec.samples * bytes_per_sample * 2;
    }
    SDL_Log("Buffer size: %d", hidden->buffer_size);

    hidden->stream_handle = snd_stream_alloc(NULL, hidden->buffer_size);
    if (hidden->stream_handle == SND_STREAM_INVALID) {
        SDL_free(hidden);
        snd_stream_shutdown();
        return SDL_SetError("Failed to allocate sound stream");
    }

    /* Allocate two aligned buffers for double buffering */
    hidden->mixbuf[0] = (Uint8 *)memalign(32, hidden->buffer_size);
    hidden->mixbuf[1] = (Uint8 *)memalign(32, hidden->buffer_size);
    hidden->silencebuf = (Uint8 *)memalign(32, hidden->buffer_size);
    if (!hidden->mixbuf[0] || !hidden->mixbuf[1] || !hidden->silencebuf) {
        SDL_free(hidden);
        snd_stream_shutdown();
        return SDL_OutOfMemory();
    }
    SDL_memset(hidden->mixbuf[0], _this->spec.silence, hidden->buffer_size);
    SDL_memset(hidden->mixbuf[1], _this->spec.silence, hidden->buffer_size);
    SDL_memset(hidden->silencebuf, _this->spec.silence, hidden->buffer_size);

    /* Set up the stream callback */
    snd_stream_reinit(hidden->stream_handle, stream_callback);

    channels = _this->spec.channels;
    frequency = _this->spec.freq;

    /* snd_stream_start_*() pre-fills the KOS stream immediately. Publish the
     * device and initial state first so PCM8 prefill gets SDL's 0x80 silence
     * instead of KOS's zero-filled fallback. */
    SDL_AtomicSet(&hidden->active_buffer, 0);
    SDL_AtomicSet(&hidden->buffer_ready, 0);
    audioDevice = _this;

    if (adpcm_stream) {
        SDL_Log("4-bit ADPCM audio format enabled\n");
        fflush(stdout);
        snd_stream_start_adpcm(hidden->stream_handle, frequency, (channels == 2) ? 1 : 0);
    } else if (_this->spec.format == AUDIO_S16LSB) {
        SDL_Log("16-bit PCM audio format enabled\n");
        snd_stream_start(hidden->stream_handle, frequency, (channels == 2) ? 1 : 0);
    } else if (_this->spec.format == AUDIO_U8) {
        SDL_Log("8-bit unsigned PCM audio format enabled\n");
        snd_stream_start_pcm8(hidden->stream_handle, frequency, (channels == 2) ? 1 : 0);
    } else {
        SDL_SetError("Unsupported audio format: %d", _this->spec.format);
        return -1;
    }

    _this->spec.userdata = (snd_stream_hnd_t*)hidden->stream_handle;
    
    // SDL_Log("stream handle: %d", hidden->stream_handle);
    // SDL_Log("_this->spec.userdata: %d", _this->spec.userdata);
    SDL_Log("Dreamcast audio driver initialized\n");
    
    /* Mark the device as enabled */
    // SDL_AtomicSet(&_this->enabled, 1);

    return 0;
}
/*
 * Close the audio device.
 */
static void DREAMCASTAUD_CloseDevice(_THIS)
{
    SDL_PrivateAudioData *hidden = (SDL_PrivateAudioData *)_this->hidden;

    SDL_Log("Closing audio device\n");

    /* Signal the SDL mixing thread to exit. */
    SDL_AtomicSet(&_this->paused, 1);
    SDL_AtomicSet(&_this->shutdown, 1);
    SDL_AtomicSet(&_this->enabled, 0);

    if (hidden) {
        SDL_AtomicSet(&hidden->buffer_ready, 0);

        /* Detach the callback first so any late poll only sees silence. */
        if (hidden->stream_handle != SND_STREAM_INVALID) {
            SDL_Log("Stopping and destroying sound stream\n");
            snd_stream_reinit(hidden->stream_handle, NULL);
            snd_stream_stop(hidden->stream_handle);
            // snd_stream_reinit(hidden->stream_handle, NULL);
            snd_stream_destroy(hidden->stream_handle);
            hidden->stream_handle = SND_STREAM_INVALID;  // Ensure the handle is invalidated
        }

        // Free allocated buffers
        if (hidden->mixbuf[0]) {
            SDL_free(hidden->mixbuf[0]);
            hidden->mixbuf[0] = NULL;
        }
        if (hidden->mixbuf[1]) {
            SDL_free(hidden->mixbuf[1]);
            hidden->mixbuf[1] = NULL;
        }
        if (hidden->silencebuf) {
            SDL_free(hidden->silencebuf);
            hidden->silencebuf = NULL;
        }

        // Free the hidden structure
        SDL_free(hidden);
        _this->hidden = NULL;  // Nullify hidden to avoid dangling pointer
    }

    // Reset the capture and audio device references
    if (_this->iscapture) {
        captureDevice = NULL;
    } else {
        audioDevice = NULL;
    }
    SDL_Log("Audio device closed\n");
    
    /* Shutdown the sound system once the stream has been stopped and destroyed. */
    snd_stream_shutdown();  // This should be called last to finalize the system shutdown


}

/*
 * Initialize the SDL2 Dreamcast audio driver.
 */
static SDL_bool DREAMCASTAUD_Init(SDL_AudioDriverImpl *impl)
{



    impl->OpenDevice  = DREAMCASTAUD_OpenDevice;
    impl->CloseDevice = DREAMCASTAUD_CloseDevice;
    impl->PlayDevice  = DREAMCASTAUD_PlayDevice;
    impl->WaitDevice  = DREAMCASTAUD_WaitDevice;
    impl->GetDeviceBuf = DREAMCASTAUD_GetDeviceBuf;
    impl->ThreadInit  = DREAMCASTAUD_ThreadInit;
    impl->ThreadDeinit = DREAMCASTAUD_ThreadDeinit;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap DREAMCASTAUD_bootstrap = {
    "dcstreamingaudio", "SDL Dreamcast Streaming Audio Driver",
    DREAMCASTAUD_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_DREAMCAST */
