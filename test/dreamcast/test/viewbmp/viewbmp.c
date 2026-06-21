#ifdef DREAMCAST
#include <kos.h>
#define BMP_PATH "/rd/Troy2024_320x240.bmp"
#else
#define BMP_PATH "data/Troy2024.bmp"
#endif
#include <SDL2/SDL.h>

static void handle_joystick_events(SDL_Joystick *joystick) {
    if (!joystick) return;

    int num_buttons = SDL_JoystickNumButtons(joystick);
    int num_axes = SDL_JoystickNumAxes(joystick);

    for (int i = 0; i < num_buttons; i++) {
        if (SDL_JoystickGetButton(joystick, i)) {
            printf("Button %d pressed\n", i);
        }
    }

    for (int i = 0; i < num_axes; i++) {
        int axis_value = SDL_JoystickGetAxis(joystick, i);
        if (axis_value != 0) {
            printf("Axis %d value: %d\n", i, axis_value);
        }
    }
}

int main(int argc, char *argv[]) {
    SDL_Window *window;
    SDL_Surface *image_surface;
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    SDL_Event event;
    int running = 1;
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");

    printf("SDL2_INIT_VIDEO\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        printf("SDL2 could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    printf("SDL_CreateWindow\n");
    window = SDL_CreateWindow("SDL2 Displaying Image", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    printf("SDL_CreateRenderer\n");
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    SDL_Log("Renderer Info:");
    SDL_Log("Name: %s", info.name);
    SDL_Log("Flags: %lu", info.flags);

    SDL_RWops *rw = SDL_RWFromFile(BMP_PATH, "rb");
    if (!rw) {
        printf("Unable to open BMP file! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("SDL_RWFromFile - %s\n", BMP_PATH);

    image_surface = SDL_LoadBMP_RW(rw, 1);
    if (!image_surface) {
        printf("Unable to load BMP file! SDL_Error: %s\n", SDL_GetError());
        SDL_RWclose(rw);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("Image surface format: %s\n", SDL_GetPixelFormatName(image_surface->format->format));

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(image_surface, SDL_PIXELFORMAT_RGB565, 0);
    SDL_FreeSurface(image_surface);
    if (!converted) {
        printf("Failed to convert surface: %s\n", SDL_GetError());
        SDL_RWclose(rw);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("After conversion: %s\n", SDL_GetPixelFormatName(converted->format->format));

    texture = SDL_CreateTextureFromSurface(renderer, converted);
    SDL_FreeSurface(converted);
    if (!texture) {
        printf("Unable to create texture from surface! SDL_Error: %s\n", SDL_GetError());
        SDL_RWclose(rw);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("SDL_CreateTextureFromSurface\n");

    SDL_Joystick *joystick = SDL_JoystickOpen(0);
    if (!joystick) {
        printf("Warning: No joystick connected!\n");
    } else {
        printf("Opened joystick: %s\n", SDL_JoystickName(joystick));
    }

    printf("running\n");
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        if (joystick) {
            handle_joystick_events(joystick);
        }

        SDL_RenderClear(renderer);

        SDL_Rect dest_rect = { 0, 0, 640, 480 };
        SDL_RenderCopyEx(renderer, texture, NULL, &dest_rect, 0, NULL, SDL_FLIP_NONE);

        SDL_RenderPresent(renderer);
    }

    if (joystick) {
        SDL_JoystickClose(joystick);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_RWclose(rw);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}