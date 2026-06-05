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


#if defined(SDL_VIDEO_DRIVER_DREAMCAST) && defined(SDL_VIDEO_OPENGL)
#include <kos.h>
#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glkos.h"

#include <kos/dbglog.h>
#define DCGL_PROBE(...) dbglog(DBG_INFO, "[dcgl_probe] " __VA_ARGS__)

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

// typedef struct {
//     int red_size;
//     int green_size;
//     int blue_size;
//     int alpha_size;
//     int depth_size;
//     int stencil_size;
//     int double_buffer;
//     int pixel_mode;
//     int Rmask;
//     int Gmask;
//     int Bmask;
//     int pitch;
// } DreamcastGLContext;

void (*DREAMCAST_GL_GetProcAddress(SDL_VideoDevice *_this, const char *proc))(void)
{
    for (int i = 0; i < sizeof(glfuncs) / sizeof(glfuncs[0]); i++) {
        if (SDL_strcmp(proc, glfuncs[i].name) == 0) {
            return (void (*)(void))glfuncs[i].addr;
        }
    }
    return NULL;
}


bool DREAMCAST_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path) {
    // _this->gl_config.driver_loaded = 1;
    return true;
}

bool DREAMCAST_GL_SwapBuffers(SDL_VideoDevice *_this, SDL_Window * window){
    static int swap_probe_count;
    if (swap_probe_count < 8) {
        DCGL_PROBE("SwapBuffers window=0x%08lx count=%d\n",
                   (unsigned long)window, swap_probe_count);
        ++swap_probe_count;
    }
    glKosSwapBuffers();
    return true;
}

typedef struct {
    GLdcConfig config;
} DreamcastGLContext;


// bool DREAMCAST_GL_Initialize(SDL_VideoDevice *_this) {
//     printf("Initializing SDL3 GLdc...\n");
//     // glKosInit();

//     // // Set default values or configure attributes as needed
//     // _this->gl_config.red_size = 8;
//     // _this->gl_config.green_size = 8;
//     // _this->gl_config.blue_size = 8;
//     // _this->gl_config.alpha_size = 8;
//     // _this->gl_config.depth_size = 32;
//     // _this->gl_config.stencil_size = 8;
//     // _this->gl_config.double_buffer = 1;

//     GLdcConfig config;
//     memset(&config, 0, sizeof(GLdcConfig)); // Ensure clean initialization
//     glKosInitConfig(&config);

//     // Manually set each field after initialization
//     config.autosort_enabled = GL_FALSE;           // Use manual sorting with depth testing
//     config.fsaa_enabled = GL_FALSE;              // Disable FSAA for better performance
//     config.internal_palette_format = GL_RGBA4;   // Use RGBA4 for paletted textures
//     // config->initial_op_capacity = 1024 * 3;
//     // config->initial_pt_capacity = 512 * 3;
//     // config->initial_tr_capacity = 1024 * 3;
//     // config->initial_immediate_capacity = 1024 * 3;
//     config.texture_twiddle = GL_FALSE;            // Enable texture twiddling for performance

//     // Apply the configuration before initializing GLdc
//     glKosInitEx(&config);

    
//     if (DREAMCAST_GL_LoadLibrary(_this, NULL) == false) {
//         return false;
//     }

//     // vid_set_mode(DM_640x480_VGA, PM_RGB888);
//     return true;
// }

void DREAMCAST_GL_Shutdown(SDL_VideoDevice *_this) {
    printf("Shutting down GLdc...\n");
    glKosShutdown();
}

SDL_GLContext DREAMCAST_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window *window) {
    DreamcastGLContext *context;

    DCGL_PROBE("CreateContext enter window=0x%08lx\n", (unsigned long)window);
    printf("Creating Dreamcast SDL3 OpenGL context...\n");
    // glKosInit();


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
    DCGL_PROBE("CreateContext before glKosInitEx\n");
    glKosInitEx(&config);
    DCGL_PROBE("CreateContext after glKosInitEx\n");

    context = (DreamcastGLContext *) SDL_calloc(1, sizeof(DreamcastGLContext));
    if (!context) {
        DCGL_PROBE("CreateContext SDL_calloc failed\n");
        SDL_OutOfMemory();
        return NULL;
    }
    context->config = config;
    // if (DREAMCAST_GL_LoadLibrary(_this, NULL) == false) {
    //     return false;
    // }
    return (SDL_GLContext) context;
}


bool DREAMCAST_GL_MakeCurrent(SDL_VideoDevice *_this, SDL_Window *window, SDL_GLContext context) {
    DCGL_PROBE("MakeCurrent window=0x%08lx context=0x%08lx\n",
               (unsigned long)window, (unsigned long)context);
    printf("OpenGL context made current for window %p\n", window);
    return true;
}

bool DREAMCAST_GL_DestroyContext(SDL_VideoDevice *_this, SDL_GLContext context) {
    DreamcastGLContext *glcontext = (DreamcastGLContext *) context;
    if (glcontext) {
        SDL_free(glcontext);
        return true;
    }
    return false;
}

// void SDL_DREAMCAST_InitGL(SDL_VideoDevice *_this) { 
//     if (DREAMCAST_GL_Initialize(_this) == false) {
//         SDL_SetError("Failed to initialize OpenGL");
//     }
// }

#endif /* SDL_VIDEO_DRIVER_DREAMCAST */
/* vi: set ts=4 sw=4 expandtab: */
