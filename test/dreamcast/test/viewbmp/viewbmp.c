#ifdef DREAMCAST
#include <kos.h>
#define BMP_PATH "/rd/Troy2024.bmp"
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

    
    printf("SDL2_INIT_VIDEO\n");
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        printf("SDL2 could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
  
    printf("SDL_CreateWindow\n"); 
    // Create a window
    window = SDL_CreateWindow("SDL2 Displaying Image", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;  
    } 
    //  printf("Select renderer\n");
    // // Loop through the available renderers
    // int render_driver_index = -1;
    // int num_render_drivers = SDL_GetNumRenderDrivers();
    // for (int i = 0; i < num_render_drivers; i++) {
    //     SDL_RendererInfo info;
    //     if (SDL_GetRenderDriverInfo(i, &info) == 0) {
    //         printf("Renderer driver %d: %s\n", i, info.name);
    //         if (strcmp(info.name, "software") == 0) {
    //             render_driver_index = i;
    //             break;
    //         }
    //     }
    // }

    // if (render_driver_index == -1) {
    //     printf("Software renderer not found!\n");
    //     SDL_DestroyWindow(window);
    //     SDL_Quit();
    //     return 1;
    // }
    printf("SDL_CreateRenderer\n"); 



    // Create a renderer
    // Set SDL hint for the renderer
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    // renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED);    
    if (!renderer) { 
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit(); 
        return 1;  
    } 

    // Load BMP file
    SDL_RWops *rw = SDL_RWFromFile(BMP_PATH, "rb");
    if (!rw) {  
        printf("Unable to open BMP file! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer); 
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("SDL_RWFromFile - %s \n", BMP_PATH);

    image_surface = SDL_LoadBMP_RW(rw, 1);
    if (!image_surface) {
        printf("Unable to load BMP file! SDL_Error: %s\n", SDL_GetError());
        SDL_RWclose(rw);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("SDL_LoadBMP_RW\n"); 

// Convert the surface to ARGB8888 format
SDL_Surface *converted_surface = SDL_ConvertSurfaceFormat(image_surface, SDL_PIXELFORMAT_RGB888, 0);
if (!converted_surface) {
    printf("Failed to convert surface format: %s\n", SDL_GetError());
    SDL_FreeSurface(image_surface);
    return -1;
}

    // Create texture from surface
    texture = SDL_CreateTextureFromSurface(renderer, converted_surface);
    SDL_FreeSurface(converted_surface); // Free the surface after creating the texture
    if (!texture) {
        printf("Unable to create texture from surface! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_RWclose(rw);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("SDL_CreateTextureFromSurface\n"); 

    // Open the first joystick
    SDL_Joystick *joystick = SDL_JoystickOpen(0);
    if (!joystick) {
        printf("Warning: No joystick connected!\n");
    } else {
        printf("Opened joystick: %s\n", SDL_JoystickName(joystick));
    }

    // Main loop
    printf("running\n");
    while (running) { 
        while (SDL_PollEvent(&event)) { 
            if (event.type == SDL_QUIT) { 
                running = 0;
            } 
        }

        // Poll joystick state
        if (joystick) {
            handle_joystick_events(joystick);
        }

        // Clear the screen 
        SDL_RenderClear(renderer);
        // Copy the texture to the renderer
        SDL_RenderCopy(renderer, texture, NULL, NULL); 
        // Present the renderer
        SDL_RenderPresent(renderer);

        SDL_Delay(16);  // approximately 60 FPS
    }

    // Clean up
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
