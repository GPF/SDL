#include <SDL3/SDL.h>
#include <kos/fs.h>
#include "../pvr_phase0_screenshot.h"

#define DT_PATH "Troy2024.dt"

typedef struct TestMemoryStream
{
    const Uint8 *bytes;
    size_t size;
    size_t offset;
} TestMemoryStream;

static Sint64 SDLCALL test_stream_size(void *userdata)
{
    return (Sint64)((TestMemoryStream *)userdata)->size;
}

static Sint64 SDLCALL test_stream_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    TestMemoryStream *stream = (TestMemoryStream *)userdata;
    Sint64 base = whence == SDL_IO_SEEK_SET ? 0 :
                  whence == SDL_IO_SEEK_CUR ? (Sint64)stream->offset :
                  whence == SDL_IO_SEEK_END ? (Sint64)stream->size : -1;
    Sint64 position = base + offset;
    if (position < 0 || (Uint64)position > stream->size) {
        return -1;
    }
    stream->offset = (size_t)position;
    return position;
}

static size_t SDLCALL test_stream_read(void *userdata, void *destination, size_t size,
                                       SDL_IOStatus *status)
{
    TestMemoryStream *stream = (TestMemoryStream *)userdata;
    size_t available = stream->size - stream->offset;
    size_t count = size < available ? size : available;
    SDL_memcpy(destination, stream->bytes + stream->offset, count);
    stream->offset += count;
    if (count < size && status) {
        *status = SDL_IO_STATUS_EOF;
    }
    return count;
}

static bool SDLCALL test_stream_close(void *userdata)
{
    (void)userdata;
    return true;
}

static SDL_IOStream *test_stream_open(TestMemoryStream *stream)
{
    SDL_IOStreamInterface iface;
    SDL_INIT_INTERFACE(&iface);
    iface.size = test_stream_size;
    iface.seek = test_stream_seek;
    iface.read = test_stream_read;
    iface.close = test_stream_close;
    return SDL_OpenIO(&iface, stream);
}

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_IOStream *io;
    SDL_Texture *texture;
    file_t dt_file;
    Sint64 dt_size;
    Uint8 *dt_bytes = NULL;
    Uint8 malformed[32] = { 'D', 'c', 'T', 'x' };
    SDL_Texture *bad_texture;
    TestMemoryStream dt_stream;
    TestMemoryStream bad_stream;
    int failures = 0;

    (void)argc;
    (void)argv;

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("pvr_dt init=FAIL %s", SDL_GetError());
        return 1;
    }
    window = SDL_CreateWindow("PVR .dt sampled texture", 640, 480, 0);
    renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    if (!renderer || SDL_strcmp(SDL_GetRendererName(renderer), "Dreamcast PVR") != 0) {
        SDL_Log("pvr_dt native_renderer=FAIL %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Log("pvr_dt renderer=%s", SDL_GetRendererName(renderer));

    dt_file = fs_open("/rd/" DT_PATH, O_RDONLY);
    dt_size = dt_file != FILEHND_INVALID ? (Sint64)fs_total(dt_file) : -1;
    if (dt_size > 0) {
        dt_bytes = (Uint8 *)SDL_malloc((size_t)dt_size);
    }
    if (dt_bytes && fs_read(dt_file, dt_bytes, (size_t)dt_size) == dt_size) {
        dt_stream.bytes = dt_bytes;
        dt_stream.size = (size_t)dt_size;
        dt_stream.offset = 0;
        io = test_stream_open(&dt_stream);
    } else {
        io = NULL;
    }
    if (dt_file != FILEHND_INVALID) {
        fs_close(dt_file);
    }
    texture = io ? SDL_LoadDreamcastTexture_IO(renderer, io, true) : NULL;
    SDL_free(dt_bytes);
    if (!texture) {
        SDL_Log("pvr_dt valid_load=FAIL %s", SDL_GetError());
        ++failures;
    } else {
        SDL_Log("pvr_dt valid_load=PASS format=%s size=%dx%d",
                SDL_GetPixelFormatName(texture->format), texture->w, texture->h);
    }

    /* A structurally impossible chunk must be rejected before texture creation. */
    bad_stream.bytes = malformed;
    bad_stream.size = sizeof(malformed);
    bad_stream.offset = 0;
    io = test_stream_open(&bad_stream);
    bad_texture = io ? SDL_LoadDreamcastTexture_IO(renderer, io, true) : NULL;
    if (!io || bad_texture) {
        SDL_Log("pvr_dt malformed_rejection=FAIL");
        ++failures;
    } else {
        SDL_Log("pvr_dt malformed_rejection=PASS");
    }
    SDL_DestroyTexture(bad_texture);

    if (texture) {
        SDL_FRect dst = { 192.0f, 112.0f, 256.0f, 256.0f };
#ifdef DT_SCALE_NEAREST
        /* Diagnostic variant only: build with -DDT_SCALE_NEAREST to check whether
         * the residual brightness offset comes from linear filtering. */
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
#endif
        for (int frame = 0; frame < 3; ++frame) {
            if (!SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer, texture, NULL, &dst) ||
                !SDL_RenderPresent(renderer)) {
                SDL_Log("pvr_dt sample_present=FAIL %s", SDL_GetError());
                ++failures;
                break;
            }
        }
#ifdef PVR_PHASE0_HARDWARE_SCREENSHOT
        if (!pvr_phase0_capture("/pc/pvr_dt_texture.ppm")) {
            SDL_Log("pvr_dt screenshot=FAIL %s", SDL_GetError());
            ++failures;
        }
#endif
        SDL_DestroyTexture(texture);
    }
    SDL_Log("pvr_dt result=%s", failures ? "FAIL" : "PASS");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures ? 1 : 0;
}
