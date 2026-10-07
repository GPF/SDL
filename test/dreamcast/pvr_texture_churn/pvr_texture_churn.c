/* Gate 8 in a loop: repeats the pvr_phase0_deferred_texture scene many times in one
 * process and checks every iteration, so a texture-lifetime race that depends on
 * timing is exercised dozens of times per run instead of once.
 *
 * Per iteration: create two 64x64 ARGB1555 checker textures (one to destroy, one
 * retained as a control), draw both for WARMUPS presents, then draw both again,
 * destroy the first BEFORE presenting, present, wait for the frame to be displayed,
 * and compare the destroyed texture's quad (and the control's) with the expected
 * checker. When a block is freed while the PVR has not started reading it, the
 * allocator's free-list pointers overwrite the first texels (the gate 8 corner).
 */
#include <SDL3/SDL.h>
#include <dc/pvr.h>
#include <stdint.h>

#define ITERATIONS 40
#define WARMUPS 3

static const SDL_FRect dst = { 128.0f, 176.0f, 128.0f, 128.0f };
static const SDL_FRect control_dst = { 384.0f, 176.0f, 128.0f, 128.0f };

static Uint16 expected_pixel(int x, int y, int control)
{
    /* Source texel (0xfc00 red / 0x801f blue, checker of 8 texels), 2x magnification. */
    const int tx = x / 2, ty = y / 2;
    const int checker = (((tx >> 3) + (ty >> 3)) & 1) != 0;
    if (control) {
        return checker ? 0x07e0 : 0xffe0; /* control: 0x83e0 green / 0xffe0 yellow in ARGB1555 */
    }
    return checker ? 0x001f : 0xf800;
}

/* The PVR dithers its RGB565 output; compare with a small per-channel tolerance. */
static int close_enough(Uint16 a, Uint16 b)
{
    const int dr = (int)(a >> 11) - (int)(b >> 11);
    const int dg = (int)((a >> 5) & 63) - (int)((b >> 5) & 63);
    const int db = (int)(a & 31) - (int)(b & 31);
    return dr >= -2 && dr <= 2 && dg >= -3 && dg <= 3 && db >= -2 && db <= 2;
}

static int count_bad(const uint16_t *fb, const SDL_FRect *r, int control, int *corner_bad)
{
    int bad = 0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const Uint16 got = fb[((int)r->y + y) * 640 + (int)r->x + x];
            if (!close_enough(got, expected_pixel(x, y, control))) {
                bad++;
                if (x < 8 && y < 2 && corner_bad) {
                    (*corner_bad)++;
                }
            }
        }
    }
    return bad;
}

int main(int argc, char **argv)
{
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DREAMCAST_PVR_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("PVR texture churn", 640, 480, 0);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    const char *name = renderer ? SDL_GetRendererName(renderer) : NULL;
    if (!name || SDL_strcmp(name, "Dreamcast PVR") != 0) {
        SDL_Log("pvr_texture_churn renderer=FAIL expected 'Dreamcast PVR', got '%s'", name ? name : "(null)");
        return 1;
    }

    static Uint16 pixels[64 * 64], control_pixels[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const int checker = (((x >> 3) + (y >> 3)) & 1) != 0;
            pixels[y * 64 + x] = checker ? 0x801f : 0xfc00;
            control_pixels[y * 64 + x] = checker ? 0x83e0 : 0xffe0;
        }
    }
    const SDL_Rect full = { 0, 0, 64, 64 };
    int dead_bad_iterations = 0, control_bad_iterations = 0, total_bad = 0;

    for (int it = 0; it < ITERATIONS; ++it) {
        SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC, 64, 64);
        SDL_Texture *ctl = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STATIC, 64, 64);

        if (!tex || !ctl || !SDL_UpdateTexture(tex, &full, pixels, 128) || !SDL_UpdateTexture(ctl, &full, control_pixels, 128)) {
            SDL_Log("pvr_texture_churn it=%d setup=FAIL %s", it, SDL_GetError());
            return 1;
        }
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(ctl, SDL_SCALEMODE_NEAREST);
        for (int pass = 0; pass <= WARMUPS; ++pass) {
            SDL_SetRenderDrawColorFloat(renderer, 1.0f, 1.0f, 1.0f, 1.0f);
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, tex, NULL, &dst);
            SDL_RenderTexture(renderer, ctl, NULL, &control_dst);
            if (pass == WARMUPS) {
                SDL_DestroyTexture(tex); /* destroyed before present: the lifetime under test */
            }
            if (!SDL_RenderPresent(renderer)) {
                SDL_Log("pvr_texture_churn it=%d present=FAIL %s", it, SDL_GetError());
                return 1;
            }
        }
        SDL_Delay(60); /* let the final frame render and flip to the display */
        {
            const uintptr_t base = 0xa5000000u;
            const uintptr_t front64 = (uintptr_t)pvr_get_front_buffer();
            const uint16_t *fb = (const uint16_t *)(base + (front64 - base) / 2);
            int corner = 0;
            const int bad = count_bad(fb, &dst, 0, &corner);
            const int cbad = count_bad(fb, &control_dst, 1, NULL);

            total_bad += bad + cbad;
            dead_bad_iterations += bad != 0;
            control_bad_iterations += cbad != 0;
            if (bad || cbad) {
                SDL_Log("pvr_texture_churn it=%d destroyed_quad_bad=%d (corner texel px=%d) control_quad_bad=%d", it, bad, corner, cbad);
            }
        }
        SDL_DestroyTexture(ctl);
    }

    SDL_Log("pvr_texture_churn iterations=%d bad_iterations=%d (corner-only-or-more) control_bad_iterations=%d total_bad_px=%d",
            ITERATIONS, dead_bad_iterations, control_bad_iterations, total_bad);
    SDL_Log("pvr_texture_churn result=%s", (dead_bad_iterations || control_bad_iterations) ? "FAIL" : "PASS");
    SDL_Delay(300);
    SDL_DestroyRenderer(renderer);
    SDL_Quit();
    return (dead_bad_iterations || control_bad_iterations) ? 1 : 0;
}
