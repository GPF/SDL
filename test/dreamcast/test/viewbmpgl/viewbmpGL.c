#include <kos.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glkos.h>

#include <stdio.h>

#define BMP_PATH "/rd/Troy2024_320X240.bmp"

GLuint LoadBMPTexture(const char *filename) {
    GLuint textureID;
    SDL_Surface *surface = SDL_LoadBMP(filename);
    if (!surface) {
        printf("Unable to load BMP file! SDL_Error: %s\n", SDL_GetError());
        return 0;
    }

    printf("Loaded BMP file successfully.\n");

    // Convert to RGB24 regardless of source format (handles BGR888, indexed, etc.)
    SDL_Surface *converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    if (!converted) {
        printf("Failed to convert surface format: %s\n", SDL_GetError());
        return 0;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STRIDE_KOS, converted->w);

glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, converted->w, converted->h, 0,
             GL_RGB, GL_UNSIGNED_BYTE, NULL);  // NULL like SDL2 render driver

// Step 2 - mirrors GL_UpdateTexture (glTexSubImage2D)
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, converted->w, converted->h,
                GL_RGB, GL_UNSIGNED_BYTE, converted->pixels);

    printf("Texture loaded: %ux%u id=%u\n", converted->w, converted->h, textureID);

    SDL_FreeSurface(converted);
    return textureID;
}

int main(int argc, char *argv[]) {
    SDL_Window *window;
    SDL_GLContext glContext;
    GLuint texture;
    SDL_Event event;
    int running = 1;
    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y, (cont_btn_callback_t)arch_exit);
    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    // Set SDL to use OpenGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    // Create a window with OpenGL context
    window = SDL_CreateWindow("SDL2 OpenGL Displaying Image", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        printf("OpenGL context could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Load the BMP texture
    texture = LoadBMPTexture(BMP_PATH);
    if (!texture) {
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Set up OpenGL state
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, 640, 480);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 640.0, 480.0, 0.0, -1.0, 1.0); // Set orthographic projection with origin at top-left
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Main loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        // Clear the screen
        glClear(GL_COLOR_BUFFER_BIT);

        // Render the texture
        glBindTexture(GL_TEXTURE_2D, texture);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);      // Top-left
        glTexCoord2f(1.0f, 0.0f); glVertex2f(320.0f, 0.0f);    // Top-right
        glTexCoord2f(1.0f, 1.0f); glVertex2f(320.0f, 240.0f);  // Bottom-right
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 240.0f);    // Bottom-left
        glEnd();

        // Swap the buffers
        SDL_GL_SwapWindow(window);

        SDL_Delay(16); // approximately 60 FPS
    }

    // Clean up
    glDeleteTextures(1, &texture);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
