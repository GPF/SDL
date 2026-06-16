#include <kos.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <GL/glkos.h>

#include <stdio.h>

#define BMP_PATH "/rd/Troy2024.bmp"

// Function to load BMP into an OpenGL texture (Dreamcast specific)
GLuint LoadBMPTexture(const char *filename) {
    GLuint textureID;
    SDL_Surface *surface = SDL_LoadBMP(filename);
    if (!surface) {
        printf("Unable to load BMP file! SDL_Error: %s\n", SDL_GetError());
        return 0;
    }

    printf("Loaded BMP file successfully.\n");

    // Convert BGR to RGB
    if (surface->format->BytesPerPixel == 3) {
        unsigned char *pixels = (unsigned char *)surface->pixels;
        for (int i = 0; i < surface->w * surface->h; ++i) {
            unsigned char temp = pixels[i * 3];
            pixels[i * 3] = pixels[i * 3 + 2];
            pixels[i * 3 + 2] = temp;
        }
    }

    glGenTextures(1, &textureID);
    printf("Generated texture ID: %u\n", textureID);

    glBindTexture(GL_TEXTURE_2D, textureID);
    printf("Bound texture ID: %u\n", textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    printf("Set texture parameters.\n");

    GLenum format = GL_RGB;
    GLenum internalFormat = GL_RGB;

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, surface->w, surface->h, 0, format, GL_UNSIGNED_BYTE, surface->pixels);
    printf("Created texture image. (Width: %d, Height: %d, Format: %d)\n", surface->w, surface->h, internalFormat);

    SDL_FreeSurface(surface);
    return textureID;
}

int main(int argc, char *argv[]) {
    SDL_Window *window;
    SDL_GLContext glContext;
    GLuint texture;
    SDL_Event event;
    int running = 1;

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
        glTexCoord2f(1.0f, 0.0f); glVertex2f(256.0f, 0.0f);    // Top-right
        glTexCoord2f(1.0f, 1.0f); glVertex2f(256.0f, 256.0f);  // Bottom-right
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 256.0f);    // Bottom-left
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
