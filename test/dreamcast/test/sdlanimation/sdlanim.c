#include <stdio.h>
#include <kos.h>
#include <SDL2/SDL.h>


/* Size of the window */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
/* Size of the grass texture picture */
#define GRASS_SIZE    16

/* In the sprite, we have 8 images of a 32x32 picture,
 * 2 images for each direction. */
/* Size of one image: */
#define SPRITE_SIZE     32
/* Order of the different directions in the picture: */
#define DIR_UP          0
#define DIR_RIGHT       1
#define DIR_DOWN        2
#define DIR_LEFT        3

/* Number of pixels for one step of the sprite */
#define SPRITE_STEP     5

void HandleGameControllerInput(SDL_GameController *controller, int *currDirection, SDL_Rect *position, int *gameover) {
    // Use game controller API for input
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A)) {
        *gameover = 1;
    }

    // Check the D-pad for direction
    if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        *currDirection = DIR_UP;
        position->y -= SPRITE_STEP;
    } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        *currDirection = DIR_DOWN;
        position->y += SPRITE_STEP;
    } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        *currDirection = DIR_LEFT;
        position->x -= SPRITE_STEP;
    } else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        *currDirection = DIR_RIGHT;
        position->x += SPRITE_STEP;
    }
}

KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

int main(int argc, char* argv[])
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *spriteTexture;
    SDL_Texture *grassTexture;
    SDL_Surface *temp;

    /* Information about the current situation of the sprite: */
    int currentDirection = DIR_RIGHT;
    int animationFlip = 0;
    SDL_Rect spritePosition;
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_TEXTURED_VIDEO");
    // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DIRECT_VIDEO");
    // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DMA_VIDEO");
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    /* initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return 1;
    }
SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(0);
char guid_str[33];
SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
SDL_Log("Joystick GUID: %s", guid_str);
   SDL_GameController *controller = NULL;
    if (SDL_NumJoysticks() > 0) {
        if (SDL_IsGameController(0)) {
            controller = SDL_GameControllerOpen(0);
            if (!controller) {
                SDL_Log("Failed to open game controller: %s", SDL_GetError());
                SDL_Quit();
                return 1;
            }
        } else {
            SDL_Log("Joystick 0 is not a compatible game controller!");
            SDL_Quit();
            return 1;
        }
    } else {
        SDL_Log("No joystick/game controller connected!");
        SDL_Quit();
        return 1;
    }
    /* create window and renderer */
    window = SDL_CreateWindow("SDL Animation", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    // Set SDL hint for the renderer
            // 
    // SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_DMA_VIDEO"); // Set for DMA mode
    // SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");    
    // renderer = SDL_CreateRenderer(window, -1,  SDL_RENDERER_PRESENTVSYNC);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    // renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

  /* Load sprite and create texture */
temp = SDL_LoadBMP("/rd/sprite2.bmp");
if (!temp) {
    SDL_Log("Failed to load sprite image: %s", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}

// Convert to ARGB8888 to support alpha
SDL_Surface* converted_surface1 = SDL_ConvertSurfaceFormat(temp, SDL_PIXELFORMAT_ARGB8888, 0);
if (!converted_surface1) {
    SDL_Log("Failed to convert sprite image format.");
    SDL_FreeSurface(temp);
    SDL_Quit();
    return 1;
}

// Extract the color of the first pixel
Uint32 *pixels = (Uint32 *)converted_surface1->pixels;
Uint32 pixel0Color = pixels[0] & 0x00FFFFFF; // Extract only RGB from pixel 0

// Loop through pixels to set transparency and swap BGR to RGB
for (int y = 0; y < converted_surface1->h; ++y) {
    for (int x = 0; x < converted_surface1->w; ++x) {
        int pixelIndex = y * converted_surface1->w + x;

        // Swap BGR to RGB
        Uint32 pixel = pixels[pixelIndex];
        Uint8 r = pixel & 0xFF;
        Uint8 g = (pixel >> 8) & 0xFF;
        Uint8 b = (pixel >> 16) & 0xFF;
        pixels[pixelIndex] = (pixel & 0xFF000000) | (r << 16) | (g << 8) | b;

        // Set transparency based on first pixel color
        if ((pixels[pixelIndex] & 0x00FFFFFF) == pixel0Color) {
            pixels[pixelIndex] = (pixels[pixelIndex] & 0x00FFFFFF); // Fully transparent
        } else {
            pixels[pixelIndex] |= 0xFF000000; // Fully opaque
        }
    }
}

// Create the texture from the modified surface
spriteTexture = SDL_CreateTextureFromSurface(renderer, converted_surface1);
SDL_FreeSurface(converted_surface1);
SDL_FreeSurface(temp);

if (!spriteTexture) {
    SDL_Log("Failed to create sprite texture: %s", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}

// Enable alpha blending for sprite
// SDL_SetTextureBlendMode(spriteTexture, SDL_BLENDMODE_BLEND);

/* Load grass and create texture */
temp = SDL_LoadBMP("/rd/grass.bmp");
if (!temp) {
    SDL_Log("Failed to load grass image: %s", SDL_GetError());
    SDL_DestroyTexture(spriteTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}

// Convert to ARGB8888 for consistency
SDL_Surface* converted_surface = SDL_ConvertSurfaceFormat(temp, SDL_PIXELFORMAT_ARGB8888, 0);
grassTexture = SDL_CreateTextureFromSurface(renderer, converted_surface);
SDL_FreeSurface(converted_surface);
SDL_FreeSurface(temp);

if (!grassTexture) {
    SDL_Log("Failed to create grass texture: %s", SDL_GetError());
    SDL_DestroyTexture(spriteTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
}

    /* set sprite position in the middle of the window */
    spritePosition.x = (SCREEN_WIDTH - SPRITE_SIZE) / 2;
    spritePosition.y = (SCREEN_HEIGHT - SPRITE_SIZE) / 2;

    int gameover = 0;

    /* main loop: check events and re-draw the window until the end */
    while (!gameover)
    {
        SDL_Event event;

        /* look for an event; possibly update the position and the shape
         * of the sprite. */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                gameover = 1;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    gameover = 1;
                }
            } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_B && event.cbutton.which == 0) {
                    SDL_Quit();
                    // exit(0);
                }
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A && event.cbutton.which == 0) {
                    gameover = 1;
                }
            }
        }


        HandleGameControllerInput(controller, &currentDirection, &spritePosition, &gameover);

        /* collide with edges of screen */
        if (spritePosition.x <= 0)
            spritePosition.x = 0;
        if (spritePosition.x >= SCREEN_WIDTH - SPRITE_SIZE) 
            spritePosition.x = SCREEN_WIDTH - SPRITE_SIZE;

        if (spritePosition.y <= 0)
            spritePosition.y = 0;
        if (spritePosition.y >= SCREEN_HEIGHT - SPRITE_SIZE) 
            spritePosition.y = SCREEN_HEIGHT - SPRITE_SIZE;

/* Clear the screen */
SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
SDL_RenderClear(renderer);

/* Draw the background */
for (int x = 0; x < SCREEN_WIDTH / GRASS_SIZE; x++) {
    for (int y = 0; y < SCREEN_HEIGHT / GRASS_SIZE; y++) {
        SDL_Rect position = {x * GRASS_SIZE, y * GRASS_SIZE, GRASS_SIZE, GRASS_SIZE};
        SDL_RenderCopy(renderer, grassTexture, NULL, &position);
    }
}

/* Enable Blending */
SDL_SetTextureBlendMode(spriteTexture, SDL_BLENDMODE_BLEND);
// SDL_SetTextureAlphaMod(spriteTexture, 255);

/* Draw the sprite */
SDL_Rect spriteImage = {
    .x = SPRITE_SIZE * (2 * currentDirection + animationFlip),
    .y = 0,
    .w = SPRITE_SIZE,
    .h = SPRITE_SIZE
};

SDL_Rect destRect = {spritePosition.x, spritePosition.y, SPRITE_SIZE, SPRITE_SIZE};
SDL_RenderCopy(renderer, spriteTexture, &spriteImage, &destRect);

animationFlip = !animationFlip; // Toggle animation state

/* Update the screen */
SDL_RenderPresent(renderer);
    }

    /* clean up */
    if (controller) {
        SDL_GameControllerClose(controller);
    }
    SDL_DestroyTexture(spriteTexture);
    SDL_DestroyTexture(grassTexture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
