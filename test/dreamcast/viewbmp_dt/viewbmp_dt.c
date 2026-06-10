#include <SDL3/SDL.h>
#include <kos.h>
#include <kos/dbgio.h>
#include <kos/dbglog.h>
#include <stdio.h>
#include <stdlib.h>
#define DT_PATH "Troy2024.dt"
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

int main(int argc, char *argv[])
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_IOStream *stream = NULL;
    SDL_Event event;
    SDL_FRect dst = { 0.0f, 0.0f, 0.0f, 0.0f };
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);

    float tex_w = 0.0f;
    float tex_h = 0.0f;
    bool running = true;
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Dreamcast SDL3 DT Viewer", 320, 410, SDL_WINDOW_OPENGL);
    if (!window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, "opengl");
    if (!renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const char *basepath = SDL_GetBasePath();
    char *filepath = NULL;

    if (!basepath) {
        SDL_Log("SDL_GetBasePath failed: %s", SDL_GetError());
        return 1;
    }

    if (SDL_asprintf(&filepath, "%s%s", basepath, DT_PATH) < 0 || !filepath) {
        SDL_Log("SDL_asprintf failed");
        SDL_free((void *)basepath);
        return 1;
    }

    stream = SDL_IOFromFile(filepath, "rb");
    if (!stream) {
        SDL_Log("Failed to open %s: %s", DT_PATH, SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    texture = SDL_LoadDreamcastTexture_IO(renderer, stream, true);
    if (!texture) {
        SDL_Log("Failed to load Dreamcast texture: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_GetTextureSize(texture, &tex_w, &tex_h)) {
        SDL_Log("Failed to query texture size: %s", SDL_GetError());
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    dst.x = (WINDOW_WIDTH - tex_w) * 0.5f;
    dst.y = (WINDOW_HEIGHT - tex_h) * 0.5f;
    dst.w = tex_w;
    dst.h = tex_h;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, &dst);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

