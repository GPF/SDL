    #ifdef DREAMCAST
    #include <kos.h>
    #define DT_PATH "/rd/Troy2024.dt"

    #else
    #define DT_PATH "data/Troy2024.dt"
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
        SDL_Texture *texture;
        SDL_Renderer *renderer;
        SDL_Event event;
        int running = 1;
        cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);

        // SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1"); // SDL2 defaults to double buffering, this shuts it off
        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_VIDEO");
        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
        // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DMA_VIDEO");
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
        // Load Dreamcast-native .dt texture.
        SDL_RWops *rw = SDL_RWFromFile(DT_PATH, "rb");
        if (!rw) {  
            printf("Unable to open DT file! SDL_Error: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer); 
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        printf("SDL_RWFromFile - %s \n", DT_PATH);

        texture = SDL_LoadDreamcastTexture_RW(renderer, rw, 1);
        if (!texture) {
            printf("Unable to create DT texture! SDL_Error: %s\n", SDL_GetError());
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        printf("SDL_LoadDreamcastTexture_RW\n");

        // Open the first joystick
        SDL_Joystick *joystick = SDL_JoystickOpen(0);
        if (!joystick) {
            printf("Warning: No joystick connected!\n");
        } else {
            printf("Opened joystick: %s\n", SDL_JoystickName(joystick));
        }

        // Main loop
    printf("running\n");
    int screenshot_taken = 0;
    int tex_w = 0;
    int tex_h = 0;
    if (SDL_QueryTexture(texture, NULL, NULL, &tex_w, &tex_h) < 0) {
        printf("SDL_QueryTexture failed! SDL_Error: %s\n", SDL_GetError());
        tex_w = 256;
        tex_h = 256;
    }
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
        
        // Render the texture at native size, centered in the window.
        SDL_Rect dest_rect = {
            (640 - tex_w) / 2,
            (480 - tex_h) / 2,
            tex_w,
            tex_h
        };
        SDL_RenderCopy(renderer, texture, NULL, &dest_rect);

        // Present the renderer
        SDL_RenderPresent(renderer);

        #ifdef DREAMCAST
        if (!screenshot_taken) {
            int screenshot_result;

            SDL_Delay(2000);
            screenshot_result = vid_screen_shot("/pc/viewbmp_dt.ppm");
            printf("vid_screen_shot('/pc/viewbmp_dt.ppm') -> %d\n", screenshot_result);
            screenshot_taken = 1;
        }
        #endif

        // Small delay to keep the loop readable on the console.
        SDL_Delay(250);
    }
        // Clean up
        if (joystick) {
            SDL_JoystickClose(joystick);
        }
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

        return 0;
    }
