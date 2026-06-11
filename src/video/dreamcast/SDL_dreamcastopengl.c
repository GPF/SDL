/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"
#include "SDL_dreamcastvideo.h"
#include "SDL_dreamcastopengl.h"
#include "SDL_video.h"


#ifdef SDL_VIDEO_DRIVER_DREAMCAST
#include <kos.h>
#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glkos.h"

#ifdef __DREAMCAST__

void glRasterPos2i(GLint x, GLint y)
{
    // TODO: Implement glRasterPos2i for Dreamcast
}

void glGetPointerv(GLenum pname, void** params)
{
    // TODO: Implement glGetPointerv for Dreamcast
}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels)
{
    // TODO: Implement glDrawPixels for Dreamcast
}

void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    // Set RGB blending
    glBlendFunc(sfactorRGB, dfactorRGB);

    // Check if alpha blending factors are non-zero
    if (sfactorAlpha != GL_ZERO && dfactorAlpha != GL_ZERO) {
        // Enable alpha test only if alpha blending is needed
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);  // This checks if alpha is greater than 0.5
    } else {
        // If no alpha blending is required, disable alpha test
        SDL_Log("Disabling alpha test");
        glDisable(GL_ALPHA_TEST);
    }
}

void glBlendEquation(GLenum mode)
{
    // TODO: Implement glBlendEquation for Dreamcast
}

#endif

typedef void (*funcptr)();
const static struct {
    char *name;
    funcptr addr;
} glfuncs[] = {
    #define DEF(func) { #func, (funcptr) &func }
    DEF(glBegin),
    DEF(glBindTexture),
    DEF(glBlendFunc),
    DEF(glClear),
    DEF(glClearColor),
    DEF(glCullFace),
    DEF(glDepthFunc),
    DEF(glDepthMask),
    DEF(glScissor),
    DEF(glColor3f),
    DEF(glColor3fv),
    DEF(glColor4f),
    DEF(glColor4fv),
    DEF(glColor4ub),
    DEF(glNormal3f),
    DEF(glDisable),
    DEF(glEnable),
    DEF(glFrontFace),
    DEF(glEnd),
    DEF(glFlush),
    DEF(glHint),
    DEF(glGenTextures),
    DEF(glGetString),
    DEF(glLoadIdentity),
    DEF(glLoadMatrixf),
    DEF(glLoadTransposeMatrixf),
    DEF(glMatrixMode),
    DEF(glMultMatrixf),
    DEF(glMultTransposeMatrixf),
    DEF(glOrtho),
    DEF(glPixelStorei),
    DEF(glPopMatrix),
    DEF(glPushMatrix),
    DEF(glRotatef),
    DEF(glScalef),
    DEF(glTranslatef),
    DEF(glTexCoord2f),
    DEF(glTexCoord2fv),
    DEF(glTexEnvi),
    DEF(glTexImage2D),
    DEF(glTexParameteri),
    DEF(glTexSubImage2D),
    DEF(glViewport),
    DEF(glShadeModel),
    DEF(glTexEnvf),
    DEF(glVertex2i),
    DEF(glVertexPointer),
    DEF(glRectf),
    DEF(glReadPixels),
    DEF(glReadBuffer),
    DEF(glLineWidth),    
    DEF(glVertex2f),
    DEF(glGetIntegerv),
    DEF(glGetFloatv),
    DEF(glGetError),
    DEF(glEnableClientState),
    DEF(glTexCoordPointer),
    DEF(glVertex3fv),
    DEF(glDrawArrays),
    DEF(glDisableClientState),
    DEF(glDeleteTextures),
    DEF(glColorPointer),
    DEF(glPointSize),    
    // These next 5 functions are stubbed out above since GLdc doesn't implement them currently
    DEF(glRasterPos2i),
    DEF(glGetPointerv),
    DEF(glDrawPixels),
    DEF(glBlendFuncSeparate),
    DEF(glBlendEquation)
    #undef DEF
};

typedef struct {
    int red_size;
    int green_size;
    int blue_size;
    int alpha_size;
    int depth_size;
    int stencil_size;
    int double_buffer;
    int pixel_mode;
    int Rmask;
    int Gmask;
    int Bmask;
    int pitch;
} DreamcastGLContext;

void *DREAMCAST_GL_GetProcAddress(_THIS, const char *proc) {
    for (int i = 0; i < sizeof(glfuncs) / sizeof(glfuncs[0]); i++) {
        if (SDL_strcmp(proc, glfuncs[i].name) == 0) {
            return glfuncs[i].addr;
        }
    }
    return NULL;
}

int DREAMCAST_GL_LoadLibrary(_THIS, const char *path) {
    _this->gl_config.driver_loaded = 1;
    return 0;
}

int DREAMCAST_GL_SwapBuffers(_THIS, SDL_Window * window){
    glKosSwapBuffers();
    return 0;
}

int DREAMCAST_GL_Initialize(_THIS) {
    printf("Initializing SDL2 GLdc...\n");
    // glKosInit();

    // // Set default values or configure attributes as needed
    // _this->gl_config.red_size = 8;
    // _this->gl_config.green_size = 8;
    // _this->gl_config.blue_size = 8;
    // _this->gl_config.alpha_size = 8;
    // _this->gl_config.depth_size = 32;
    // _this->gl_config.stencil_size = 8;
    // _this->gl_config.double_buffer = 1;

    GLdcConfig config;
    memset(&config, 0, sizeof(GLdcConfig)); // Ensure clean initialization
    glKosInitConfig(&config);

    // Manually set each field after initialization
    config.autosort_enabled = GL_FALSE;           // Use manual sorting with depth testing
    config.fsaa_enabled = GL_FALSE;              // Disable FSAA for better performance
    config.internal_palette_format = GL_RGBA4;   // Use RGBA4 for paletted textures
    // config->initial_op_capacity = 1024 * 3;
    // config->initial_pt_capacity = 512 * 3;
    // config->initial_tr_capacity = 1024 * 3;
    // config->initial_immediate_capacity = 1024 * 3;
    config.texture_twiddle = GL_FALSE;            // Enable texture twiddling for performance

    // Apply the configuration before initializing GLdc
    glKosInitEx(&config);

    
    if (DREAMCAST_GL_LoadLibrary(_this, NULL) < 0) {
        return -1;
    }

    // vid_set_mode(DM_640x480_VGA, PM_RGB888);
    return 0;
}

void DREAMCAST_GL_Shutdown(_THIS) {
    printf("Shutting down GLdc...\n");
    glKosShutdown();
}

SDL_GLContext DREAMCAST_GL_CreateContext(_THIS, SDL_Window *window) {
    DreamcastGLContext *context;

    // Initialize the OpenGL driver if it hasn't been initialized
    if (DREAMCAST_GL_Initialize(_this) < 0) {
        SDL_SetError("Failed to initialize OpenGL");
        return NULL;
    }

    printf("Creating Dreamcast SDL2 OpenGL context...\n");

    context = (DreamcastGLContext *) SDL_calloc(1, sizeof(DreamcastGLContext));
    if (!context) {
        SDL_OutOfMemory();
        return NULL;
    }

    // Store the GL attributes in the context
    // context->red_size = 8;   // You can still set these if you want to keep them in context
    // context->green_size = 8;
    // context->blue_size = 8;
    // context->alpha_size = 8;
    // context->depth_size = 32;
    // context->stencil_size = 8;
    // context->double_buffer = 1;

    return (SDL_GLContext) context;
}


int DREAMCAST_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context) {
    DreamcastGLContext *glcontext = (DreamcastGLContext *) context;
    glKosInit();
    // Validate the context
    if (!glcontext) {
        SDL_SetError("Invalid OpenGL context");
        return -1;
    }
    
    // Check if the window is valid
    if (!window) {
        SDL_SetError("Invalid window");
        return -1;
    }
    
    // // Set up the GLdcConfig structure
    // GLdcConfig config;
    // memset(&config, 0, sizeof(config)); // Initialize config to zero

    // // Configure the GLdcConfig with values from your context
    // config.autosort_enabled = GL_FALSE; // or GL_TRUE based on your needs
    // config.fsaa_enabled = GL_FALSE; // or GL_TRUE if you want anti-aliasing
    // config.internal_palette_format = GL_RGBA4; // Adjust if needed
    // config.initial_op_capacity = 1024; // Example values
    // config.initial_tr_capacity = 1024;
    // config.initial_pt_capacity = 1024;
    // config.initial_immediate_capacity = 1024;
    // config.texture_twiddle = GL_TRUE; // Adjust based on your needs
    
    // // Initialize GL with the configured settings
    // glKosInitEx(&config);

    printf("OpenGL context made current for window %p\n", window);
    return 0;
}

void DREAMCAST_GL_DeleteContext(_THIS, SDL_GLContext context) {
    DreamcastGLContext *glcontext = (DreamcastGLContext *) context;
    if (glcontext) {
        SDL_free(glcontext);
    }
}

void SDL_DREAMCAST_InitGL(_THIS) { 
    if (DREAMCAST_GL_Initialize(_this) < 0) {
        SDL_SetError("Failed to initialize OpenGL");
    }
}

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */
/* vi: set ts=4 sw=4 expandtab: */
