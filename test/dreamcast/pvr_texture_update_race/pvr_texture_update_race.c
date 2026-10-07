/* CPU upload vs. in-flight render: uploads a texture right after a render-to-texture
 * scene that sampled it, and checks that the target kept the colour it was drawn with.
 *
 * Each frame f: draw the streaming texture X (solid palette[f % 7]) many times into a
 * render target T so that scene lasts a while, switch back to the screen (finishing
 * that scene), then IMMEDIATELY upload X = palette[(f + 1) % 7], draw T to the screen
 * as a quad, draw the frame number as an 8-square marker, present, and sample the
 * displayed page. The marker names the frame on screen and the quad must show that
 * frame's colour. If the upload lands while the PVR is still rendering the T scene
 * (or has not started it), T shows the next frame's colour.
 *
 * Uploads after RenderPresent returns are not the hazard: the presentation scene's
 * TA entry already waits for the frame scene's render to complete.
 */
#include <SDL3/SDL.h>
#include <dc/pvr.h>
#include <stdint.h>
#include <time.h>

#define FRAMES 250 /* marker is 8 bits, so frames stay below 256 */
#define TEX 64
#define QX 224
#define QY 144
#define QS 192
#define SAMPLES 24

/* Saturated, widely separated colours: the neighbour frame's colour is never
 * within the output dither tolerance of the expected one. */
static const Uint16 palette[7] = { 0xf800, 0x07e0, 0x001f, 0xffe0, 0x07ff, 0xf81f, 0x8410 };

static int close_enough(Uint16 a, Uint16 b)
{
    const int dr = (int)(a >> 11) - (int)(b >> 11);
    const int dg = (int)((a >> 5) & 63) - (int)((b >> 5) & 63);
    const int db = (int)(a & 31) - (int)(b & 31);
    return dr >= -2 && dr <= 2 && dg >= -3 && dg <= 3 && db >= -2 && db <= 2;
}

typedef struct
{
    int marker_ok;
    int marker;
    int bad;
} Check;

static Check checks[FRAMES];

static void check_front(int f)
{
    const uintptr_t base = 0xa5000000u;
    const uintptr_t front64 = (uintptr_t)pvr_get_front_buffer();
    const uint16_t *fb = (const uint16_t *)(base + (front64 - base) / 2);
    Check *c = &checks[f];
    int marker = 0;

    c->marker_ok = 1;
    for (int bit = 0; bit < 8; ++bit) {
        const Uint16 p = fb[8 * 640 + 8 + bit * 16 + 8];
        if (p == 0xffff) {
            marker |= 1 << bit;
        } else if (p != 0x0000) {
            c->marker_ok = 0;
        }
    }
    c->marker = marker;
    c->bad = 0;
    if (c->marker_ok) {
        const Uint16 expect = palette[marker % 7];
        for (int i = 0; i < SAMPLES; ++i) {
            const int x = QX + 6 + (i * 53) % (QS - 12);
            const int y = QY + 6 + (i * 41) % (QS - 12);
            if (!close_enough(fb[y * 640 + x], expect)) {
                c->bad++;
            }
        }
    }
}

int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("PVR texture update race", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    const char *name = renderer ? SDL_GetRendererName(renderer) : NULL;
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_texture_update_race renderer=FAIL expected 'Dreamcast PVR', got '%s'", name ? name : "(null)");
        return 1;
    }
    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, TEX, TEX);
    if (!tex) {
        SDL_Log("pvr_texture_update_race CreateTexture=FAIL %s", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, 128, 128);
    if (!target) {
        SDL_Log("pvr_texture_update_race CreateTexture(target)=FAIL %s", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(target, SDL_SCALEMODE_NEAREST);

    static Uint16 pixels[TEX * TEX];
    const SDL_Rect full = { 0, 0, TEX, TEX };
    const SDL_FRect dst = { QX, QY, QS, QS };

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    for (int i = 0; i < TEX * TEX; ++i) {
        pixels[i] = palette[0];
    }
    if (!SDL_UpdateTexture(tex, &full, pixels, TEX * 2)) {
        SDL_Log("pvr_texture_update_race initial upload=FAIL %s", SDL_GetError());
        return 1;
    }
    for (int f = 0; f < FRAMES; ++f) {
        const SDL_FRect whole = { 0, 0, 128, 128 };

        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        for (int k = 0; k < 40; ++k) {
            SDL_RenderTexture(renderer, tex, NULL, &whole); /* a render that lasts a while */
        }
        SDL_SetRenderTarget(renderer, NULL); /* finishes the target scene */

        for (int i = 0; i < TEX * TEX; ++i) {
            pixels[i] = palette[(f + 1) % 7];
        }
        if (!SDL_UpdateTexture(tex, &full, pixels, TEX * 2)) { /* next frame's colour, immediately */
            SDL_Log("pvr_texture_update_race frame=%d UpdateTexture=FAIL %s", f, SDL_GetError());
            return 1;
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, target, NULL, &dst);
        for (int bit = 0; bit < 8; ++bit) {
            const SDL_FRect m = { (float)(8 + bit * 16), 0, 16, 16 };
            const Uint8 v = ((f >> bit) & 1) ? 255 : 0;
            SDL_SetRenderDrawColor(renderer, v, v, v, 255);
            SDL_RenderFillRect(renderer, &m);
        }
        if (!SDL_RenderPresent(renderer)) {
            SDL_Log("pvr_texture_update_race frame=%d present=FAIL %s", f, SDL_GetError());
            return 1;
        }
        check_front(f);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    SDL_Log("pvr_texture_update_race %.2f ms per frame",
            ((double)(t_end.tv_sec - t_start.tv_sec) * 1e3 + (double)(t_end.tv_nsec - t_start.tv_nsec) / 1e6) / FRAMES);
    int bad_frames = 0, bad_samples = 0, unreadable = 0, shown = 0;
    for (int f = 8; f < FRAMES; ++f) { /* let the pipeline fill first */
        if (!checks[f].marker_ok) {
            unreadable++;
        } else {
            shown++;
            if (checks[f].bad) {
                bad_frames++;
                bad_samples += checks[f].bad;
                if (bad_frames <= 6) {
                    SDL_Log("pvr_texture_update_race corrupt: check %d showed frame %d with %d/%d wrong samples", f,
                            checks[f].marker, checks[f].bad, SAMPLES);
                }
            }
        }
    }
    SDL_Log("pvr_texture_update_race frames=%d checked=%d unreadable_markers=%d corrupt_frames=%d bad_samples=%d",
            FRAMES - 8, shown, unreadable, bad_frames, bad_samples);
    SDL_Log("pvr_texture_update_race result=%s", (bad_frames || unreadable) ? "FAIL" : "PASS");
    SDL_Delay(300);
    SDL_DestroyTexture(target);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_Quit();
    return (bad_frames || unreadable) ? 1 : 0;
}
