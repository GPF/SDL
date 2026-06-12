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
        cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);

        // SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1"); // SDL2 defaults to double buffering, this shuts it off
        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_VIDEO");
        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DMA_VIDEO");
        SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
        // SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
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
        printf("SDL_CreateRenderer\n"); 



        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DMA_VIDEO");
        // Create a renderer
        // Set SDL hint for the renderer
        // SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");    
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC); 
        // SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "opengl"); 
        // renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED| SDL_RENDERER_PRESENTVSYNC);
        // renderer = SDL_CreateRenderer(window, 0, SDL_RENDERER_ACCELERATED| SDL_RENDERER_PRESENTVSYNC);    
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
        // SDL_SetColorKey(image_surface, 1, *((Uint8 *)image_surface->pixels));
    // Print out the pixel format of the loaded image surface
    const char* format_name = SDL_GetPixelFormatName(image_surface->format->format);
    printf("Image surface format: %s\n", format_name);    
    // Uint32 transparentColor = *(Uint32 *)image_surface->pixels;
    // SDL_SetColorKey(image_surface->pixels, SDL_TRUE, transparentColor);
    // Convert the surface to RGB565 for the Dreamcast OpenGL path.
    SDL_Surface *converted_surface = SDL_ConvertSurfaceFormat(image_surface, SDL_PIXELFORMAT_RGB565, 0);
    if (!converted_surface) {
        printf("Failed to convert surface format: %s\n", SDL_GetError());
        SDL_FreeSurface(image_surface);
        return -1;
    }

    format_name = SDL_GetPixelFormatName(converted_surface->format->format);
    printf("converted_surface surface format: %s\n", format_name); 
    // Manually adjust the pixel data to swap BGR to RGB
    SDL_SetColorKey(converted_surface, 1, *((Uint8 *)converted_surface->pixels));
    // Uint32 *pixels = (Uint32 *)converted_surface->pixels;
    // for (int y = 0; y < converted_surface->h; ++y) {
    //     for (int x = 0; x < converted_surface->w; ++x) {
    //         Uint32 pixel = pixels[y * converted_surface->w + x];

    //         // Extract ARGB values
    //         Uint8 a = (pixel >> 24) & 0xFF;
    //         Uint8 r = (pixel >> 16) & 0xFF;
    //         Uint8 g = (pixel >> 8) & 0xFF;
    //         Uint8 b = pixel & 0xFF;

    //         // Swap B and R
    //         Uint32 corrected_pixel = (a << 24) | (b << 16) | (g << 8) | r;
    //         pixels[y * converted_surface->w + x] = corrected_pixel;
    //     }
    // }
    format_name = SDL_GetPixelFormatName(converted_surface->format->format);
    printf("converted_surface surface format after extract argb values: %s\n", format_name);    
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
    int k = 1; // Toggle variable to switch flipping
    int fl = SDL_FLIP_NONE;
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
        
        // Create a destination rectangle
        SDL_Rect dest_rect = { 0, 0, 320, 240 }; // Set to desired dimensions
        
        if (k) {
            fl = SDL_FLIP_VERTICAL;
        } else {
            fl = SDL_FLIP_NONE;
        }
        // Render the texture with the adjusted rectangle
        SDL_RenderCopyEx(renderer, texture, NULL, &dest_rect, 0, NULL, fl);

        // Present the renderer
        SDL_RenderPresent(renderer);

        // Toggle k to flip texture on next frame
        k = !k;

        // Small delay so the flip is visible.
        SDL_Delay(250);
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
