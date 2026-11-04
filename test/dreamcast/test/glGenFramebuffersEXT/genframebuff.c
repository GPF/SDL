#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <kos.h>

#define GAME_W 320
#define GAME_H 240
#define WIN_W  640
#define WIN_H  480

static uint16_t g_pixels[GAME_W * GAME_H];

static void draw_scene(uint16_t *pixels, int x, int y)
{
    for (int py = 0; py < GAME_H; ++py) {
        for (int px = 0; px < GAME_W; ++px) {
            uint16_t c = 0x0010; /* dark blue background */

            if (py < 16) {
                c = 0x07FF; /* cyan bar */
            }

            if (px >= x && px < x + 24 && py >= y && py < y + 24) {
                c = 0xF800; /* red square */
            }

            pixels[py * GAME_W + px] = c;
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window *win = SDL_CreateWindow(
        "Render Texture Demo",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer *ren = SDL_CreateRenderer(
        win,
        -1,
        SDL_RENDERER_ACCELERATED
    );

    SDL_Texture *scene = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        GAME_W, GAME_H
    );
    SDL_SetTextureScaleMode(scene, SDL_ScaleModeNearest);

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

        if (x < 0 || x > GAME_W - 24) {
            vx = -vx;
            x += vx;
        }
        if (y < 0 || y > GAME_H - 24) {
            vy = -vy;
            y += vy;
        }

        draw_scene(g_pixels, x, y);
        SDL_UpdateTexture(scene, NULL, g_pixels, GAME_W * (int)sizeof(uint16_t));

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        SDL_Rect dst = { 0, 0, WIN_W, WIN_H };
        SDL_RenderCopy(ren, scene, NULL, &dst);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(scene);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
