/*  DREAMCAST
 *IAN MICHEAL Ported SDL+OPENGL USING SDL[DREAMHAL][GLDC][KOS2.0]2021
 * Cleaned and tested on dreamcast hardware by Ianmicheal
 *		This Code Was Created By Pet & Commented/Cleaned Up By Jeff Molofee
 *		If You've Found This Code Useful, Please Let Me Know.
 *		Visit NeHe Productions At http://nehe.gamedev.net
 */
//Troy Davis GPF ported to dreamcast SDL2+opengl 
#include <math.h>            // Header File For Windows Math Library
#include <stdio.h>           // Header File For Standard Input/Output
#include "GL/gl.h"
#include "GL/glu.h"
#include "SDL2/SDL.h"

#define FPS 60
Uint32 waittime = 1000.0f / FPS;
Uint32 framestarttime = 0;
Sint32 delaytime;

float xrot = 0.0f, yrot = 0.0f, zrot = 0.0f;
float xspeed = 0.0f, yspeed = 0.0f, zspeed = 0.0f;
const float ROTATION_SPEED = 1000.0f;

GLuint texture[1];

// Load BMP and convert to texture
SDL_Surface *LoadBMP(char *filename) {
    Uint8 *rowhi, *rowlo;
    Uint8 *tmpbuf, tmpch;
    SDL_Surface *image = SDL_LoadBMP(filename);
    if (!image) {
        fprintf(stderr, "Unable to load %s: %s\n", filename, SDL_GetError());
        return NULL;
    }
const char* format_name = SDL_GetPixelFormatName(image->format->format);
printf("Image surface format: %s\n", format_name);  

    printf("SDL_LoadBMP_RW\n"); 
    SDL_SetColorKey(image, 1, *((Uint8 *)image->pixels));
 
    tmpbuf = (Uint8 *)malloc(image->pitch);
    if (!tmpbuf) {
        fprintf(stderr, "Out of memory\n");
        return NULL;
    }
    rowhi = (Uint8 *)image->pixels;
    rowlo = rowhi + (image->h * image->pitch) - image->pitch;
    for (int i = 0; i < image->h / 2; ++i) {
        for (int j = 0; j < image->w; ++j) {
            tmpch = rowhi[j * 3];
            rowhi[j * 3] = rowhi[j * 3 + 2];
            rowhi[j * 3 + 2] = tmpch;
            tmpch = rowlo[j * 3];
            rowlo[j * 3] = rowlo[j * 3 + 2];
            rowlo[j * 3 + 2] = tmpch;
        }
        memcpy(tmpbuf, rowhi, image->pitch);
        memcpy(rowhi, rowlo, image->pitch);
        memcpy(rowlo, tmpbuf, image->pitch);
        rowhi += image->pitch;
        rowlo -= image->pitch;
    }
    free(tmpbuf);
    return image;
}

// Load textures
void LoadGLTextures() {
    SDL_Surface *image1 = LoadBMP("/rd/NeHe.bmp");
    if (!image1) {
        SDL_Quit();
        exit(1);
    }

    glGenTextures(1, &texture[0]);
    glBindTexture(GL_TEXTURE_2D, texture[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // Create an empty texture first
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image1->w, image1->h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    // Update the texture with the image data
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image1->w, image1->h, GL_RGB, GL_UNSIGNED_BYTE, image1->pixels);    

    SDL_FreeSurface(image1);
}


/* A general OpenGL initialization function.  Sets all of the initial parameters. */
void InitGL(int Width, int Height)            // We call this right after our OpenGL window is created.
{
    glViewport(0, 0, Width, Height);
    LoadGLTextures();
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.0f, 0.0f, 1.0f, 0.0f);
    glClearDepth(1.0);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f, (GLfloat)Width / (GLfloat)Height, 0.1f, 100.0f);

    glMatrixMode(GL_MODELVIEW);
}

void DrawGLScene()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    glTranslatef(0.0f,0.0f,-5.0f);
    
    glRotatef(xrot,1.0f,0.0f,0.0f);
    glRotatef(yrot,0.0f,1.0f,0.0f);
    glRotatef(zrot,0.0f,0.0f,1.0f);

    glBindTexture(GL_TEXTURE_2D, texture[0]);

    glBegin(GL_QUADS);
    
    // Front Face
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
    
    // Back Face
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    
    // Top Face
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
    
    // Bottom Face
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
    
    // Right face
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
    
    // Left Face
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
    
    glEnd();

    SDL_GL_SwapWindow(SDL_GL_GetCurrentWindow());
}


void HandleJoystickInput(SDL_Joystick *joystick, float *xspeed, float *yspeed, const float ROTATION_SPEED) {
    // Check the D-pad hat position (using the first hat)
    Uint8 hat = SDL_JoystickGetHat(joystick, 0);

    if (hat & SDL_HAT_UP) {
        *xspeed -= ROTATION_SPEED * 0.101f;
    } else if (hat & SDL_HAT_DOWN) {
        *xspeed += ROTATION_SPEED * 0.101f;
    }

    if (hat & SDL_HAT_LEFT) {
        *yspeed -= ROTATION_SPEED * 0.101f;
    } else if (hat & SDL_HAT_RIGHT) {
        *yspeed += ROTATION_SPEED * 0.101f;
    }
}

int main(int argc, char **argv) {
SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    SDL_Window *window = SDL_CreateWindow("Joystick Rotation", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_OPENGL);
    SDL_GLContext glContext = SDL_GL_CreateContext(window);

    SDL_Joystick *joystick = SDL_JoystickOpen(0); // Open the first joystick
    if (!joystick) {
        fprintf(stderr, "Unable to open joystick: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    InitGL(640, 480);
    int done = 0;
    Uint32 lastTime = SDL_GetTicks();

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
        }
    HandleJoystickInput(joystick, &xspeed, &yspeed, ROTATION_SPEED);

        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        xrot += xspeed * deltaTime;
        yrot += yspeed * deltaTime;
        zrot += zspeed * deltaTime;

        DrawGLScene();

        Uint32 frameTime = SDL_GetTicks() - currentTime;
        if (frameTime < waittime) {
            SDL_Delay(waittime - frameTime);
        }
    }

    SDL_JoystickClose(joystick);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
