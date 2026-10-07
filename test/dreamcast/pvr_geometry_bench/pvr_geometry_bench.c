/* CPU submission cost of geometry workloads for the PVR renderer (sprites, rects,
 * points, lines, triangles). Prints ms per frame spent submitting the workload and the
 * cost per primitive. No pass/fail: it exists to compare library builds.
 *
 * Each frame starts the scene with a clear and a flush, which absorbs KallistiOS's wait
 * for the previous render, and then times only the flush that submits the workload to
 * the TA. Timing whole frames instead would hide the cost under the 16.7 ms vsync
 * interval for light workloads and mix in GPU time for heavy ones. Counts stay below
 * what the 512 KB vertex buffer holds (about 2700 rectangles); more overflows it.
 * Clock: clock_gettime(CLOCK_MONOTONIC); SDL_GetTicksNS was wrong on Dreamcast before
 * the timer fix. */
#include <SDL3/SDL.h>
#include <time.h>
#define FR 60
static unsigned long long now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (unsigned long long)t.tv_sec*1000000000ull+t.tv_nsec; }
static SDL_Renderer *r; static SDL_Texture *tex;
static SDL_FRect spr[400], rects[2400];
static SDL_FPoint pts[2400];
static SDL_Vertex verts[4500];

static double run(const char *name, int nsprites, int nrects, int npoints, int nlines, int ntris, int units)
{
    unsigned long long total_ns = 0;
    unsigned long long frame0 = now_ns();

    for (int f = 0; f < FR; ++f) {
        unsigned long long t0;

        SDL_SetRenderDrawColor(r, 0x20, 0x20, 0x40, 0xFF);
        SDL_RenderClear(r);
        SDL_FlushRenderer(r); /* starts the scene; KallistiOS waits for the previous render here */
        t0 = now_ns();
        if (nrects) { SDL_SetRenderDrawColor(r, 255, 128, 0, 255); SDL_RenderFillRects(r, rects, nrects); }
        if (npoints) { SDL_SetRenderDrawColor(r, 255, 0, 0, 255); SDL_RenderPoints(r, pts, npoints); }
        if (nlines) { SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
            for (int k = 0; k < nlines; ++k) { SDL_RenderLine(r, 32, 32 + k, 606, 446 - k); SDL_RenderLine(r, 606, 32 + k, 32, 446 - k); } }
        if (ntris) SDL_RenderGeometry(r, NULL, verts, ntris * 3, NULL, 0);
        for (int i = 0; i < nsprites; ++i) SDL_RenderTexture(r, tex, NULL, &spr[i]);
        SDL_FlushRenderer(r); /* submits the workload to the TA */
        total_ns += now_ns() - t0;
        SDL_RenderPresent(r);
    }
    {
        const double ms = total_ns / 1e6 / FR;
        const double frame_ms = (now_ns() - frame0) / 1e6 / FR;

        if (units > 0) {
            SDL_Log("gbench %-32s submit %7.2f ms/frame  %6.2f us per unit (%d units)  [frame %.2f ms]", name, ms, ms * 1000.0 / units, units, frame_ms);
        } else {
            SDL_Log("gbench %-32s submit %7.2f ms/frame  [frame %.2f ms]", name, ms, frame_ms);
        }
        return ms;
    }
}
int main(int argc, char **argv)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *w = SDL_CreateWindow("g", 640, 480, 0); r = SDL_CreateRenderer(w, NULL);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Log("gbench renderer=%s", SDL_GetRendererName(r));
    SDL_Surface *s = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888); SDL_FillSurfaceRect(s, NULL, 0x80FF8000);
    tex = SDL_CreateTextureFromSurface(r, s); SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 400; ++i) { spr[i].x = (float)(i * 37 % 610); spr[i].y = (float)(i * 23 % 450); spr[i].w = spr[i].h = 16; }
    for (int i = 0; i < 2400; ++i) { rects[i].x = (float)(i * 41 % 630); rects[i].y = (float)(i * 29 % 470); rects[i].w = rects[i].h = 4; }
    for (int i = 0; i < 2400; ++i) { pts[i].x = (float)(i % 600 + 20); pts[i].y = (float)(i / 600 * 40 + (i * 7) % 40 + 20); }
    for (int i = 0; i < 1500; ++i) {
        float x = (float)(i * 43 % 600), y = (float)(i * 31 % 440);
        for (int k = 0; k < 3; ++k) { verts[i*3+k].color = (SDL_FColor){ 1, (float)k * 0.5f, 0.2f, 1 }; }
        verts[i*3+0].position = (SDL_FPoint){ x, y }; verts[i*3+1].position = (SDL_FPoint){ x + 20, y }; verts[i*3+2].position = (SDL_FPoint){ x, y + 20 };
    }
    for (int i = 0; i < 3; ++i) { SDL_RenderClear(r); SDL_RenderPresent(r); }
    run("400 sprites 16x16", 400,0,0,0,0, 400);
    run("2400 fill rects 4x4", 0,2400,0,0,0, 2400);
    run("2400 points", 0,0,2400,0,0, 2400);
    run("2 diagonal lines (~1150 px)", 0,0,0,1,0, 1150);
    run("2 diagonal line pairs (~2300 px)", 0,0,0,2,0, 2300);
    run("1500 triangles (geometry)", 0,0,0,0,1500, 1500);
    run("testsprite-like (100 spr+lines)", 100,0,0,1,0, 0);
    SDL_Log("gbench done"); SDL_Delay(300); SDL_Quit(); return 0;
}
