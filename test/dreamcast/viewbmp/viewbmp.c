#include <SDL3/SDL.h>
#include <kos.h>

#define BMP_PATH "/rd/Troy2024_320x240.bmp"

int main(int argc, char *argv[]) {
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);
    // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_VIDEO");
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) == false) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Dreamcast SDL3 Viewer", 640, 480, 0);
    if (!window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, "opengl");
    if (!renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Surface *surface = SDL_LoadBMP(BMP_PATH);
    if (!surface) {
        SDL_Log("Failed to load BMP: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Optional: Convert to preferred format if needed
    SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(surface);
    if (!converted) {
        SDL_Log("Failed to convert BMP: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, converted);
    SDL_DestroySurface(converted);


    if (!texture) {
        SDL_Log("Failed to create texture: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Event e;
    bool running = true;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
