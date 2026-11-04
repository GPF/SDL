#include <SDL2/SDL.h>
#include <stdbool.h>
#include <kos.h>

#define WIN_W 640
#define WIN_H 480
#define TARGET_W 320
#define TARGET_H 240

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "SDL2 Target Texture Demo",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE
    );
    if (!ren) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_Texture *target = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_TARGET,
        TARGET_W, TARGET_H
    );
    if (!target) {
        SDL_Log("SDL_CreateTexture(target) failed: %s", SDL_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(target, SDL_ScaleModeNearest);

    SDL_Log("Created target texture %dx%d", TARGET_W, TARGET_H);

    int x = 0;
    int y = 0;
    int vx = 2;
    int vy = 1;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
        }

        x += vx;
        y += vy;
        if (x < 0 || x > TARGET_W - 24) {
            vx = -vx;
            x += vx;
        }
        if (y < 0 || y > TARGET_H - 24) {
            vy = -vy;
            y += vy;
        }

        if (SDL_SetRenderTarget(ren, target) < 0) {
            SDL_Log("SDL_SetRenderTarget(target) failed: %s", SDL_GetError());
            break;
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        SDL_Rect bar = { 0, 0, TARGET_W, 16 };
        SDL_Rect square = { x, y, 24, 24 };

        SDL_SetRenderDrawColor(ren, 0, 0, 64, 255);
        SDL_RenderFillRect(ren, NULL);

        SDL_SetRenderDrawColor(ren, 0, 255, 255, 255);
        SDL_RenderFillRect(ren, &bar);

        SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
        SDL_RenderFillRect(ren, &square);

        if (SDL_SetRenderTarget(ren, NULL) < 0) {
            SDL_Log("SDL_SetRenderTarget(NULL) failed: %s", SDL_GetError());
            break;
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        SDL_Rect dst = { 0, 0, WIN_W, WIN_H };
        SDL_RenderCopyEx(ren, target, NULL, &dst, 0.0, NULL, SDL_FLIP_VERTICAL);
        SDL_RenderPresent(ren);

        SDL_Delay(16);
    }

    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
