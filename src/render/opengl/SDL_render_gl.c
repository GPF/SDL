/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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

#if SDL_VIDEO_RENDER_OGL
#include "SDL_hints.h"
#include "../../video/SDL_sysvideo.h" /* For SDL_GL_SwapWindowWithResult */
#include "SDL_opengl.h"
#include "../SDL_sysrender.h"
#include "SDL_shaders_gl.h"
#include "../../SDL_utils_c.h"

#ifdef __MACOSX__
#include <OpenGL/OpenGL.h>
#endif

#ifdef SDL_VIDEO_VITA_PVR_OGL
#include <GL/gl.h>
#include <GL/glext.h>
#endif

// #if defined(SDL_VIDEO_DRIVER_DREAMCAST) && defined(SDL_VIDEO_OPENGL)
// #include <kos.h>
// #include "GL/gl.h"
// #include "GL/glu.h"
// #include "GL/glkos.h"
// #endif

#ifdef __DREAMCAST__
#ifndef GL_ARGB1555_KOS
#define GL_ARGB1555_KOS 0xEF42
#endif
#ifndef GL_RGB565_KOS
#define GL_RGB565_KOS 0xEF40
#endif
#ifndef GL_UNSIGNED_SHORT_5_6_5_TWID_KOS
#define GL_UNSIGNED_SHORT_5_6_5_TWID_KOS 0xEEE0
#endif
#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS
#define GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS 0xEEE2
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS
#define GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS 0xEEE3
#endif
#ifndef GL_ARGB4444_KOS
#define GL_ARGB4444_KOS 0xEF41
#endif
#ifndef GL_RGB565_TWID_KOS
#define GL_RGB565_TWID_KOS 0xEF43
#endif
#ifndef GL_ARGB4444_TWID_KOS
#define GL_ARGB4444_TWID_KOS 0xEF44
#endif
#ifndef GL_ARGB1555_TWID_KOS
#define GL_ARGB1555_TWID_KOS 0xEF45
#endif
#ifndef GL_TEXTURE_STRIDE_KOS
#define GL_TEXTURE_STRIDE_KOS 0xEF52
#endif
#ifndef GL_COMPRESSED_RGB_565_VQ_KOS
#define GL_COMPRESSED_RGB_565_VQ_KOS 0xEEE4
#endif
#ifndef GL_COMPRESSED_ARGB_1555_VQ_KOS
#define GL_COMPRESSED_ARGB_1555_VQ_KOS 0xEEE6
#endif
#ifndef GL_COMPRESSED_ARGB_4444_VQ_KOS
#define GL_COMPRESSED_ARGB_4444_VQ_KOS 0xEEE7
#endif
#ifndef GL_COMPRESSED_RGB_565_VQ_TWID_KOS
#define GL_COMPRESSED_RGB_565_VQ_TWID_KOS 0xEEE8
#endif
#ifndef GL_COMPRESSED_ARGB_1555_VQ_TWID_KOS
#define GL_COMPRESSED_ARGB_1555_VQ_TWID_KOS 0xEEEA
#endif
#ifndef GL_COMPRESSED_ARGB_4444_VQ_TWID_KOS
#define GL_COMPRESSED_ARGB_4444_VQ_TWID_KOS 0xEEEB
#endif
#ifndef GL_COMPRESSED_RGB_565_VQ_MIPMAP_KOS
#define GL_COMPRESSED_RGB_565_VQ_MIPMAP_KOS 0xEEEC
#endif
#ifndef GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_KOS
#define GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_KOS 0xEEED
#endif
#ifndef GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_KOS
#define GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_KOS 0xEEEE
#endif
#ifndef GL_COMPRESSED_RGB_565_VQ_MIPMAP_TWID_KOS
#define GL_COMPRESSED_RGB_565_VQ_MIPMAP_TWID_KOS 0xEEEF
#endif
#ifndef GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_TWID_KOS
#define GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_TWID_KOS 0xEEF0
#endif
#ifndef GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_TWID_KOS
#define GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_TWID_KOS 0xEEF1
#endif

extern void APIENTRY glCompressedTexImage2DARB(GLenum target, GLint level, GLenum internalFormat,
                                              GLsizei width, GLsizei height, GLint border,
                                              GLsizei imageSize, const GLvoid *data);
#endif

/* To prevent unnecessary window recreation,
 * these should match the defaults selected in SDL_GL_ResetAttributes
 */

#define RENDERER_CONTEXT_MAJOR 2
#define RENDERER_CONTEXT_MINOR 1

/* OpenGL renderer implementation */

/* Details on optimizing the texture path on Mac OS X:
   http://developer.apple.com/library/mac/#documentation/GraphicsImaging/Conceptual/OpenGL-MacProgGuide/opengl_texturedata/opengl_texturedata.html
*/

static const float inv255f = 1.0f / 255.0f;

typedef struct GL_FBOList GL_FBOList;

struct GL_FBOList
{
    Uint32 w, h;
    GLuint FBO;
    GL_FBOList *next;
};

typedef struct
{
    SDL_bool viewport_dirty;
    SDL_Rect viewport;
    SDL_Texture *texture;
    SDL_Texture *target;
    int drawablew;
    int drawableh;
    SDL_BlendMode blend;
    GL_Shader shader;
    SDL_bool cliprect_enabled_dirty;
    SDL_bool cliprect_enabled;
    SDL_bool cliprect_dirty;
    SDL_Rect cliprect;
    SDL_bool texturing;
    SDL_bool vertex_array;
    SDL_bool color_array;
    SDL_bool texture_array;
    Uint32 color;
    Uint32 clear_color;
} GL_DrawStateCache;

typedef struct
{
    SDL_GLContext context;

    SDL_bool debug_enabled;
    SDL_bool GL_ARB_debug_output_supported;
    int errors;
    char **error_messages;
    GLDEBUGPROCARB next_error_callback;
    GLvoid *next_error_userparam;

    GLenum textype;

    SDL_bool GL_ARB_texture_non_power_of_two_supported;
    SDL_bool GL_ARB_texture_rectangle_supported;
    SDL_bool GL_EXT_framebuffer_object_supported;
    GL_FBOList *framebuffers;

    /* OpenGL functions */
#define SDL_PROC(ret, func, params) ret (APIENTRY *func) params;
#include "SDL_glfuncs.h"
#undef SDL_PROC

    /* Multitexture support */
    SDL_bool GL_ARB_multitexture_supported;
    PFNGLACTIVETEXTUREARBPROC glActiveTextureARB;
    GLint num_texture_units;

    PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
    PFNGLDELETEFRAMEBUFFERSEXTPROC glDeleteFramebuffersEXT;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;
    PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
    PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;

    /* Shader support */
    GL_ShaderContext *shaders;

    GL_DrawStateCache drawstate;
} GL_RenderData;

typedef struct
{
    GLuint texture;
    GLfloat texw;
    GLfloat texh;
    GLenum format;
    GLenum formattype;
    GL_Shader shader;
    void *pixels;
    int pitch;
    SDL_Rect locked_rect;

#if SDL_HAVE_YUV
    /* YUV texture support */
    SDL_bool yuv;
    SDL_bool nv12;
    GLuint utexture;
    GLuint vtexture;
#endif

    GL_FBOList *fbo;
#ifdef __DREAMCAST__
    Uint32 original_format;
    SDL_bool strided;
#endif
} GL_TextureData;

#ifdef __DREAMCAST__
typedef struct SDL_DreamcastDtHeader
{
    char fourcc[4];
    Uint32 chunk_size;
    Uint8 version;
    Uint8 header_size;
    Uint8 codebook_size;
    Uint8 colors_used;
    Uint16 width_pixels;
    Uint16 height_pixels;
    Uint32 pvr_type;
    Uint32 pad1;
    Uint32 pad2;
    /* The DcTx header is 32 bytes total; this reserved word keeps the
       SDL header layout aligned with the GLdc loader and the on-disk file. */
    Uint32 pad3;
} SDL_DreamcastDtHeader;

#define SDL_DC_DT_HEADER_SIZE 32
#define SDL_DC_DT_MIPMAP_SHIFT 31
#define SDL_DC_DT_VQ_SHIFT 30
#define SDL_DC_DT_PIXEL_FORMAT_SHIFT 27
#define SDL_DC_DT_PIXEL_FORMAT_MASK 0x7
#define SDL_DC_DT_NOT_TWIDDLED_SHIFT 26
#define SDL_DC_DT_STRIDE_SHIFT 25
#define SDL_DC_MAX_STRIDE_WIDTH 992

enum
{
    SDL_DC_DT_FMT_ARGB1555 = 0,
    SDL_DC_DT_FMT_RGB565 = 1,
    SDL_DC_DT_FMT_ARGB4444 = 2
};

static SDL_bool GL_DreamcastDtCompressed(Uint32 pvr_type)
{
    return (SDL_bool)((pvr_type >> SDL_DC_DT_VQ_SHIFT) & 1);
}

static SDL_bool GL_DreamcastDtMipmapped(Uint32 pvr_type)
{
    return (SDL_bool)((pvr_type >> SDL_DC_DT_MIPMAP_SHIFT) & 1);
}

static SDL_bool GL_DreamcastDtTwiddled(Uint32 pvr_type)
{
    return (SDL_bool)(((pvr_type >> SDL_DC_DT_NOT_TWIDDLED_SHIFT) & 1) == 0);
}

static SDL_bool GL_DreamcastDtStrided(Uint32 pvr_type)
{
    return (SDL_bool)((pvr_type >> SDL_DC_DT_STRIDE_SHIFT) & 1);
}

static SDL_bool GL_DreamcastIsPowerOfTwo(int value)
{
    return (SDL_bool)(value > 0 && ((value & (value - 1)) == 0));
}

static SDL_bool GL_DreamcastCanUseStridedTexture(const SDL_Texture *texture)
{
    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        return SDL_FALSE;
    }

    if (GL_DreamcastIsPowerOfTwo(texture->w) && GL_DreamcastIsPowerOfTwo(texture->h)) {
        return SDL_FALSE;
    }

    if ((texture->w <= 0) || ((texture->w % 32) != 0) || (texture->w > SDL_DC_MAX_STRIDE_WIDTH)) {
        return SDL_FALSE;
    }

    switch (texture->format) {
    case SDL_PIXELFORMAT_YV12:
    case SDL_PIXELFORMAT_IYUV:
    case SDL_PIXELFORMAT_NV12:
    case SDL_PIXELFORMAT_NV21:
        return SDL_FALSE;
    default:
        break;
    }

    return SDL_TRUE;
}

static int GL_DreamcastDtPixelFormat(Uint32 pvr_type)
{
    return (int)((pvr_type >> SDL_DC_DT_PIXEL_FORMAT_SHIFT) & SDL_DC_DT_PIXEL_FORMAT_MASK);
}

static Uint32 GL_DreamcastDtSDLFormat(int dt_format)
{
    switch (dt_format) {
    case SDL_DC_DT_FMT_RGB565:
        return SDL_PIXELFORMAT_RGB565;
    case SDL_DC_DT_FMT_ARGB1555:
        return SDL_PIXELFORMAT_ARGB1555;
    case SDL_DC_DT_FMT_ARGB4444:
        return SDL_PIXELFORMAT_ARGB4444;
    default:
        return SDL_PIXELFORMAT_UNKNOWN;
    }
}

static GLenum GL_DreamcastDtInternalFormat(int dt_format, SDL_bool compressed, SDL_bool twiddled, SDL_bool mipmapped)
{
    if (compressed) {
        switch (dt_format) {
        case SDL_DC_DT_FMT_RGB565:
            if (mipmapped) {
                return twiddled ? GL_COMPRESSED_RGB_565_VQ_MIPMAP_TWID_KOS : GL_COMPRESSED_RGB_565_VQ_MIPMAP_KOS;
            }
            return twiddled ? GL_COMPRESSED_RGB_565_VQ_TWID_KOS : GL_COMPRESSED_RGB_565_VQ_KOS;
        case SDL_DC_DT_FMT_ARGB1555:
            if (mipmapped) {
                return twiddled ? GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_TWID_KOS : GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_KOS;
            }
            return twiddled ? GL_COMPRESSED_ARGB_1555_VQ_TWID_KOS : GL_COMPRESSED_ARGB_1555_VQ_KOS;
        case SDL_DC_DT_FMT_ARGB4444:
            if (mipmapped) {
                return twiddled ? GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_TWID_KOS : GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_KOS;
            }
            return twiddled ? GL_COMPRESSED_ARGB_4444_VQ_TWID_KOS : GL_COMPRESSED_ARGB_4444_VQ_KOS;
        default:
            return 0;
        }
    }

    switch (dt_format) {
    case SDL_DC_DT_FMT_RGB565:
        return twiddled ? GL_RGB565_TWID_KOS : GL_RGB565_KOS;
    case SDL_DC_DT_FMT_ARGB1555:
        return twiddled ? GL_ARGB1555_TWID_KOS : GL_ARGB1555_KOS;
    case SDL_DC_DT_FMT_ARGB4444:
        return twiddled ? GL_ARGB4444_TWID_KOS : GL_ARGB4444_KOS;
    default:
        return 0;
    }
}

static void GL_DreamcastDtFormatAndType(int dt_format, SDL_bool twiddled, GLenum *format, GLenum *type)
{
    switch (dt_format) {
    case SDL_DC_DT_FMT_RGB565:
        *format = GL_RGB;
        *type = twiddled ? GL_UNSIGNED_SHORT_5_6_5_TWID_KOS : GL_UNSIGNED_SHORT_5_6_5;
        break;
    case SDL_DC_DT_FMT_ARGB1555:
        *format = GL_BGRA;
        *type = twiddled ? GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS : GL_UNSIGNED_SHORT_1_5_5_5_REV;
        break;
    case SDL_DC_DT_FMT_ARGB4444:
        *format = GL_BGRA;
        *type = twiddled ? GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS : GL_UNSIGNED_SHORT_4_4_4_4_REV;
        break;
    default:
        *format = 0;
        *type = 0;
        break;
    }
}
#endif

SDL_FORCE_INLINE const char *
GL_TranslateError(GLenum error)
{
#define GL_ERROR_TRANSLATE(e) \
    case e:                   \
        return #e;
    switch (error) {
        GL_ERROR_TRANSLATE(GL_INVALID_ENUM)
        GL_ERROR_TRANSLATE(GL_INVALID_VALUE)
        GL_ERROR_TRANSLATE(GL_INVALID_OPERATION)
        GL_ERROR_TRANSLATE(GL_OUT_OF_MEMORY)
        GL_ERROR_TRANSLATE(GL_NO_ERROR)
        GL_ERROR_TRANSLATE(GL_STACK_OVERFLOW)
        GL_ERROR_TRANSLATE(GL_STACK_UNDERFLOW)
        GL_ERROR_TRANSLATE(GL_TABLE_TOO_LARGE)
    default:
        return "UNKNOWN";
    }
#undef GL_ERROR_TRANSLATE
}

SDL_FORCE_INLINE void
GL_ClearErrors(SDL_Renderer *renderer)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;

    if (!data->debug_enabled) {
        return;
    }
    if (data->GL_ARB_debug_output_supported) {
        if (data->errors) {
            int i;
            for (i = 0; i < data->errors; ++i) {
                SDL_free(data->error_messages[i]);
            }
            SDL_free(data->error_messages);

            data->errors = 0;
            data->error_messages = NULL;
        }
    } else if (data->glGetError) {
        while (data->glGetError() != GL_NO_ERROR) {
            /* continue; */
        }
    }
}

SDL_FORCE_INLINE int
GL_CheckAllErrors(const char *prefix, SDL_Renderer *renderer, const char *file, int line, const char *function)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;
    int ret = 0;

    if (!data->debug_enabled) {
        return 0;
    }
    if (data->GL_ARB_debug_output_supported) {
        if (data->errors) {
            int i;
            for (i = 0; i < data->errors; ++i) {
                SDL_SetError("%s: %s (%d): %s %s", prefix, file, line, function, data->error_messages[i]);
                ret = -1;
            }
            GL_ClearErrors(renderer);
        }
    } else {
        /* check gl errors (can return multiple errors) */
        for (;;) {
            GLenum error = data->glGetError();
            if (error != GL_NO_ERROR) {
                if (prefix == NULL || prefix[0] == '\0') {
                    prefix = "generic";
                }
                SDL_SetError("%s: %s (%d): %s %s (0x%X)", prefix, file, line, function, GL_TranslateError(error), error);
                ret = -1;
            } else {
                break;
            }
        }
    }
    return ret;
}

#if 0
#define GL_CheckError(prefix, renderer)
#else
#define GL_CheckError(prefix, renderer) GL_CheckAllErrors(prefix, renderer, SDL_FILE, SDL_LINE, SDL_FUNCTION)
#endif

static int GL_LoadFunctions(GL_RenderData *data)
{
#ifdef __SDL_NOGETPROCADDR__
#define SDL_PROC(ret, func, params) data->func = func;
#else
    int retval = 0;
#define SDL_PROC(ret, func, params)                                                           \
    do {                                                                                      \
        data->func = SDL_GL_GetProcAddress(#func);                                            \
        if (!data->func) {                                                                    \
            retval = SDL_SetError("Couldn't load GL function %s: %s", #func, SDL_GetError()); \
        }                                                                                     \
    } while (0);
#endif /* __SDL_NOGETPROCADDR__ */

#include "SDL_glfuncs.h"
#undef SDL_PROC
    return retval;
}

static int GL_ActivateRenderer(SDL_Renderer *renderer)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;

    if (SDL_GL_GetCurrentContext() != data->context) {
        if (SDL_GL_MakeCurrent(renderer->window, data->context) < 0) {
            return -1;
        }
    }

    GL_ClearErrors(renderer);

    return 0;
}

static void APIENTRY GL_HandleDebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const char *message, const void *userParam)
{
    SDL_Renderer *renderer = (SDL_Renderer *)userParam;
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;

    if (type == GL_DEBUG_TYPE_ERROR_ARB) {
        /* Record this error */
        int errors = data->errors + 1;
        char **error_messages = SDL_realloc(data->error_messages, errors * sizeof(*data->error_messages));
        if (error_messages) {
            data->errors = errors;
            data->error_messages = error_messages;
            data->error_messages[data->errors - 1] = SDL_strdup(message);
        }
    }

    /* If there's another error callback, pass it along, otherwise log it */
    if (data->next_error_callback) {
        data->next_error_callback(source, type, id, severity, length, message, data->next_error_userparam);
    } else {
        if (type == GL_DEBUG_TYPE_ERROR_ARB) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", message);
        } else {
            SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "%s", message);
        }
    }
}

static GL_FBOList *GL_GetFBO(GL_RenderData *data, Uint32 w, Uint32 h)
{
    GL_FBOList *result = data->framebuffers;

    while (result && ((result->w != w) || (result->h != h))) {
        result = result->next;
    }

    if (!result) {
        result = SDL_malloc(sizeof(GL_FBOList));
        if (result) {
            result->w = w;
            result->h = h;
            data->glGenFramebuffersEXT(1, &result->FBO);
            result->next = data->framebuffers;
            data->framebuffers = result;
        }
    }
    return result;
}

static void GL_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    /* If the window x/y/w/h changed at all, assume the viewport has been
     * changed behind our backs. x/y changes might seem weird but viewport
     * resets have been observed on macOS at minimum!
     */
    if (event->event == SDL_WINDOWEVENT_SIZE_CHANGED ||
        event->event == SDL_WINDOWEVENT_MOVED) {
        GL_RenderData *data = (GL_RenderData *)renderer->driverdata;
        data->drawstate.viewport_dirty = SDL_TRUE;
    }
}

static int GL_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    SDL_GL_GetDrawableSize(renderer->window, w, h);
    return 0;
}

static GLenum GetBlendFunc(SDL_BlendFactor factor)
{
    switch (factor) {
    case SDL_BLENDFACTOR_ZERO:
        return GL_ZERO;
    case SDL_BLENDFACTOR_ONE:
        return GL_ONE;
    case SDL_BLENDFACTOR_SRC_COLOR:
        return GL_SRC_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR:
        return GL_ONE_MINUS_SRC_COLOR;
    case SDL_BLENDFACTOR_SRC_ALPHA:
        return GL_SRC_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:
        return GL_ONE_MINUS_SRC_ALPHA;
    case SDL_BLENDFACTOR_DST_COLOR:
        return GL_DST_COLOR;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR:
        return GL_ONE_MINUS_DST_COLOR;
    case SDL_BLENDFACTOR_DST_ALPHA:
        return GL_DST_ALPHA;
    case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA:
        return GL_ONE_MINUS_DST_ALPHA;
    default:
        return GL_INVALID_ENUM;
    }
}

static GLenum GetBlendEquation(SDL_BlendOperation operation)
{
    switch (operation) {
    case SDL_BLENDOPERATION_ADD:
        return GL_FUNC_ADD;
    case SDL_BLENDOPERATION_SUBTRACT:
        return GL_FUNC_SUBTRACT;
    case SDL_BLENDOPERATION_REV_SUBTRACT:
        return GL_FUNC_REVERSE_SUBTRACT;
    case SDL_BLENDOPERATION_MINIMUM:
        return GL_MIN;
    case SDL_BLENDOPERATION_MAXIMUM:
        return GL_MAX;
    default:
        return GL_INVALID_ENUM;
    }
}

static SDL_bool GL_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    SDL_BlendFactor srcColorFactor = SDL_GetBlendModeSrcColorFactor(blendMode);
    SDL_BlendFactor srcAlphaFactor = SDL_GetBlendModeSrcAlphaFactor(blendMode);
    SDL_BlendOperation colorOperation = SDL_GetBlendModeColorOperation(blendMode);
    SDL_BlendFactor dstColorFactor = SDL_GetBlendModeDstColorFactor(blendMode);
    SDL_BlendFactor dstAlphaFactor = SDL_GetBlendModeDstAlphaFactor(blendMode);
    SDL_BlendOperation alphaOperation = SDL_GetBlendModeAlphaOperation(blendMode);

    if (GetBlendFunc(srcColorFactor) == GL_INVALID_ENUM ||
        GetBlendFunc(srcAlphaFactor) == GL_INVALID_ENUM ||
        GetBlendEquation(colorOperation) == GL_INVALID_ENUM ||
        GetBlendFunc(dstColorFactor) == GL_INVALID_ENUM ||
        GetBlendFunc(dstAlphaFactor) == GL_INVALID_ENUM ||
        GetBlendEquation(alphaOperation) == GL_INVALID_ENUM) {
        return SDL_FALSE;
    }
    if (colorOperation != alphaOperation) {
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

SDL_FORCE_INLINE SDL_bool
convert_format(GL_RenderData *renderdata, Uint32 pixel_format,
               GLint *internalFormat, GLenum *format, GLenum *type)
{
    switch (pixel_format) {
#ifdef __DREAMCAST__
    case SDL_PIXELFORMAT_RGB24:
        *internalFormat = GL_RGB;
        *format = GL_RGB;
        *type = GL_UNSIGNED_BYTE;
        break;
    case SDL_PIXELFORMAT_RGB888:
        *internalFormat = GL_RGB;
        *format = GL_RGB;
        *type = GL_UNSIGNED_BYTE;  // Dreamcast-specific handling for RGB888
        break;
    case SDL_PIXELFORMAT_RGB565:
        *internalFormat = GL_RGB565_KOS;
        *format = GL_RGB;
        *type = GL_UNSIGNED_SHORT_5_6_5;  // Dreamcast-specific handling for RGB565
        break;
    case SDL_PIXELFORMAT_ARGB1555:
        *internalFormat = GL_ARGB1555_KOS;
        *format = GL_BGRA; 
        *type = GL_UNSIGNED_SHORT_1_5_5_5_REV; // Dreamcast-specific handling for ARGB1555
        break;
    case SDL_PIXELFORMAT_ARGB4444:
        *internalFormat = GL_RGBA;
        *format = GL_BGRA;
        *type = GL_UNSIGNED_SHORT_4_4_4_4_REV;  // Dreamcast-specific handling for ARGB8888
        break;        
    case SDL_PIXELFORMAT_ARGB8888:
        *internalFormat = GL_RGBA;
        *format = GL_RGBA;
        *type = GL_UNSIGNED_BYTE;  // Dreamcast-specific handling for ARGB8888
        break;
#else
    case SDL_PIXELFORMAT_ARGB8888:
    case SDL_PIXELFORMAT_RGB888:
        *internalFormat = GL_RGBA8;
        *format = GL_BGRA;
        *type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
#endif
    case SDL_PIXELFORMAT_ABGR8888:
    case SDL_PIXELFORMAT_BGR888:
        *internalFormat = GL_RGBA8;
        *format = GL_RGBA;
        *type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
    case SDL_PIXELFORMAT_YV12:
    case SDL_PIXELFORMAT_IYUV:
    case SDL_PIXELFORMAT_NV12:
    case SDL_PIXELFORMAT_NV21:
        *internalFormat = GL_LUMINANCE;
        *format = GL_LUMINANCE;
        *type = GL_UNSIGNED_BYTE;
        break;
#ifdef __MACOSX__
    case SDL_PIXELFORMAT_UYVY:
        *internalFormat = GL_RGB8;
        *format = GL_YCBCR_422_APPLE;
        *type = GL_UNSIGNED_SHORT_8_8_APPLE;
        break;
#endif
    default:
        return SDL_FALSE;
    }
    return SDL_TRUE;
}

static int GL_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GL_RenderData *renderdata = (GL_RenderData *)renderer->driverdata;
    const GLenum textype = renderdata->textype;
    GL_TextureData *data;
    GLint internalFormat;
    GLenum format, type;
    int texture_w, texture_h;
    GLenum scaleMode;
#ifdef __DREAMCAST__
    Uint32 dreamcast_original_format = texture->format;
    SDL_bool dreamcast_strided = SDL_FALSE;
    SDL_Log("GL_CreateTexture: incoming format=%s", SDL_GetPixelFormatName(texture->format));
#endif

    GL_ActivateRenderer(renderer);

    renderdata->drawstate.texture = NULL; /* we trash this state. */
    renderdata->drawstate.texturing = SDL_FALSE; /* we trash this state. */

    if (texture->access == SDL_TEXTUREACCESS_TARGET &&
        !renderdata->GL_EXT_framebuffer_object_supported) {
        return SDL_SetError("Render targets not supported by OpenGL");
    }

#ifdef __DREAMCAST__
    if (texture->access != SDL_TEXTUREACCESS_STREAMING &&
        texture->format != SDL_PIXELFORMAT_RGB565 &&
        texture->format != SDL_PIXELFORMAT_ARGB1555 &&
        texture->format != SDL_PIXELFORMAT_ARGB4444 &&
        texture->format != SDL_PIXELFORMAT_RGB24) {
        texture->format = SDL_PIXELFORMAT_ARGB1555;
    }
    /* GLdc has no GL_BGR — remap BGR24 to RGB24 so GL_UpdateTexture
       converts it via SDL_ConvertPixels before upload */
    if (texture->format == SDL_PIXELFORMAT_BGR24) {
        texture->format = SDL_PIXELFORMAT_RGB24;
    }

    SDL_Log("GL_CreateTexture: after DC format fix=%s", SDL_GetPixelFormatName(texture->format));
#endif
    if (!convert_format(renderdata, texture->format, &internalFormat,
                        &format, &type)) {
        return SDL_SetError("Texture format %s not supported by OpenGL",
                            SDL_GetPixelFormatName(texture->format));
    }

    data = (GL_TextureData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }

#ifdef __DREAMCAST__
    data->original_format = dreamcast_original_format;
    data->strided = SDL_FALSE;
#endif

    if (texture->access == SDL_TEXTUREACCESS_STREAMING) {
        size_t size;
        data->pitch = texture->w * SDL_BYTESPERPIXEL(texture->format);
        size = (size_t)texture->h * data->pitch;
        if (texture->format == SDL_PIXELFORMAT_YV12 ||
            texture->format == SDL_PIXELFORMAT_IYUV) {
            /* Need to add size for the U and V planes */
            size += 2 * ((texture->h + 1) / 2) * ((data->pitch + 1) / 2);
        }
        if (texture->format == SDL_PIXELFORMAT_NV12 ||
            texture->format == SDL_PIXELFORMAT_NV21) {
            /* Need to add size for the U/V plane */
            size += 2 * ((texture->h + 1) / 2) * ((data->pitch + 1) / 2);
        }
        data->pixels = SDL_calloc(1, size);
        if (!data->pixels) {
            SDL_free(data);
            return SDL_OutOfMemory();
        }
    }

    if (texture->access == SDL_TEXTUREACCESS_TARGET) {
        data->fbo = GL_GetFBO(renderdata, texture->w, texture->h);
    } else {
        data->fbo = NULL;
    }

    GL_CheckError("", renderer);
    renderdata->glGenTextures(1, &data->texture);
    if (GL_CheckError("glGenTextures()", renderer) < 0) {
        if (data->pixels) {
            SDL_free(data->pixels);
        }
        SDL_free(data);
        return -1;
    }
    texture->driverdata = data;

    if (renderdata->GL_ARB_texture_non_power_of_two_supported) {
        texture_w = texture->w;
        texture_h = texture->h;
        data->texw = 1.0f;
        data->texh = 1.0f;
    } else if (renderdata->GL_ARB_texture_rectangle_supported) {
        texture_w = texture->w;
        texture_h = texture->h;
        data->texw = (GLfloat)texture_w;
        data->texh = (GLfloat)texture_h;
    } else {
#ifdef __DREAMCAST__
        GLint maxSize;
        int isPowerOfTwoWidth;
        int isPowerOfTwoHeight;

        renderdata->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);

        isPowerOfTwoWidth = (texture->w & (texture->w - 1)) == 0;
        isPowerOfTwoHeight = (texture->h & (texture->h - 1)) == 0;

        dreamcast_strided = GL_DreamcastCanUseStridedTexture(texture);

        if (dreamcast_strided) {
            texture_w = texture->w;
            texture_h = texture->h;
            data->texw = 1.0f;
            data->texh = 1.0f;
            data->strided = SDL_TRUE;

            SDL_Log("Dreamcast: using GL_KOS_texture_stride: w=%d, h=%d",
                    texture_w, texture_h);
        } else if (!isPowerOfTwoWidth || !isPowerOfTwoHeight) {
            int oldtexture_w = texture->w;
            int oldtexture_h = texture->h;
            int texturebpp;
            int newStride;

            // Adjust the width and height to the nearest power-of-two size
            texture_w = (texture->w <= maxSize) ? SDL_powerof2(texture->w) : maxSize;
            texture_h = (texture->h <= maxSize) ? SDL_powerof2(texture->h) : maxSize;

            // Normalize texw and texh after adjustment to power-of-two
            data->texw = (GLfloat)(texture->w) / texture_w;
            data->texh = (GLfloat)(texture->h) / texture_h;

            SDL_Log("Dreamcast: Adjusted texture size to power-of-two: oldw=%d, oldh=%d, w=%d, h=%d, maxSize=%d\n", oldtexture_w, oldtexture_h, texture_w, texture_h, maxSize);

            texture->scaleMode = SDL_ScaleModeNearest;

            texturebpp = SDL_BYTESPERPIXEL(texture->format);
            newStride = texture_w * texturebpp;
            renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, (newStride / texturebpp));
        } else {
            texture_w = texture->w;
            texture_h = texture->h;
            data->texw = 1.0f;
            data->texh = 1.0f;
        }
#else
        texture_w = SDL_powerof2(texture->w);
        texture_h = SDL_powerof2(texture->h);
        data->texw = (GLfloat)(texture->w) / texture_w;
        data->texh = (GLfloat)texture->h / texture_h;
#endif
}

    data->format = format;
    data->formattype = type;
    scaleMode = (texture->scaleMode == SDL_ScaleModeNearest) ? GL_NEAREST : GL_LINEAR;
    renderdata->glEnable(textype);
    renderdata->glBindTexture(textype, data->texture);
    renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER, scaleMode);
    renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER, scaleMode);
    /* According to the spec, CLAMP_TO_EDGE is the default for TEXTURE_RECTANGLE
       and setting it causes an INVALID_ENUM error in the latest NVidia drivers.
    */
    if (textype != GL_TEXTURE_RECTANGLE_ARB) {
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_S,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_T,
                                    GL_CLAMP_TO_EDGE);
    }
#ifdef __DREAMCAST__
    if (data->strided) {
        renderdata->glTexParameteri(textype, GL_TEXTURE_STRIDE_KOS, texture_w);
    }
#endif
#ifdef __MACOSX__
#ifndef GL_TEXTURE_STORAGE_HINT_APPLE
#define GL_TEXTURE_STORAGE_HINT_APPLE 0x85BC
#endif
#ifndef STORAGE_CACHED_APPLE
#define STORAGE_CACHED_APPLE 0x85BE
#endif
#ifndef STORAGE_SHARED_APPLE
#define STORAGE_SHARED_APPLE 0x85BF
#endif
    if (texture->access == SDL_TEXTUREACCESS_STREAMING) {
        renderdata->glTexParameteri(textype, GL_TEXTURE_STORAGE_HINT_APPLE,
                                    GL_STORAGE_SHARED_APPLE);
    } else {
        renderdata->glTexParameteri(textype, GL_TEXTURE_STORAGE_HINT_APPLE,
                                    GL_STORAGE_CACHED_APPLE);
    }
    if (texture->access == SDL_TEXTUREACCESS_STREAMING && texture->format == SDL_PIXELFORMAT_ARGB8888 && (texture->w % 8) == 0) {
        renderdata->glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
        renderdata->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                  (data->pitch / SDL_BYTESPERPIXEL(texture->format)));
        renderdata->glTexImage2D(textype, 0, internalFormat, texture_w,
                                 texture_h, 0, format, type, data->pixels);
        renderdata->glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
    } else
#endif
    {
        renderdata->glTexImage2D(textype, 0, internalFormat, texture_w,
                                 texture_h, 0, format, type, NULL);
    }
    renderdata->glDisable(textype);
    if (GL_CheckError("glTexImage2D()", renderer) < 0) {
        return -1;
    }

#if SDL_HAVE_YUV
    if (texture->format == SDL_PIXELFORMAT_YV12 ||
        texture->format == SDL_PIXELFORMAT_IYUV) {
        data->yuv = SDL_TRUE;

        renderdata->glGenTextures(1, &data->utexture);
        renderdata->glGenTextures(1, &data->vtexture);

        renderdata->glBindTexture(textype, data->utexture);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER,
                                    scaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER,
                                    scaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_S,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_T,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexImage2D(textype, 0, internalFormat, (texture_w + 1) / 2,
                                 (texture_h + 1) / 2, 0, format, type, NULL);

        renderdata->glBindTexture(textype, data->vtexture);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER,
                                    scaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER,
                                    scaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_S,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_T,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexImage2D(textype, 0, internalFormat, (texture_w + 1) / 2,
                                 (texture_h + 1) / 2, 0, format, type, NULL);
    }

    if (texture->format == SDL_PIXELFORMAT_NV12 ||
        texture->format == SDL_PIXELFORMAT_NV21) {
        data->nv12 = SDL_TRUE;

        renderdata->glGenTextures(1, &data->utexture);
        renderdata->glBindTexture(textype, data->utexture);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER,
                                    scaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER,
                                    scaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_S,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexParameteri(textype, GL_TEXTURE_WRAP_T,
                                    GL_CLAMP_TO_EDGE);
        renderdata->glTexImage2D(textype, 0, GL_LUMINANCE_ALPHA, (texture_w + 1) / 2,
                                 (texture_h + 1) / 2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);
    }
#endif

    if (texture->format == SDL_PIXELFORMAT_ABGR8888 || texture->format == SDL_PIXELFORMAT_ARGB8888) {
        data->shader = SHADER_RGBA;
    } else {
        data->shader = SHADER_RGB;
    }

#if SDL_HAVE_YUV
    if (data->yuv || data->nv12) {
        switch (SDL_GetYUVConversionModeForResolution(texture->w, texture->h)) {
        case SDL_YUV_CONVERSION_JPEG:
            if (data->yuv) {
                data->shader = SHADER_YUV_JPEG;
            } else if (texture->format == SDL_PIXELFORMAT_NV12) {
                data->shader = SHADER_NV12_JPEG;
            } else {
                data->shader = SHADER_NV21_JPEG;
            }
            break;
        case SDL_YUV_CONVERSION_BT601:
            if (data->yuv) {
                data->shader = SHADER_YUV_BT601;
            } else if (texture->format == SDL_PIXELFORMAT_NV12) {
                if (SDL_GetHintBoolean("SDL_RENDER_OPENGL_NV12_RG_SHADER", SDL_FALSE)) {
                    data->shader = SHADER_NV12_RG_BT601;
                } else {
                    data->shader = SHADER_NV12_RA_BT601;
                }
            } else {
                data->shader = SHADER_NV21_BT601;
            }
            break;
        case SDL_YUV_CONVERSION_BT709:
            if (data->yuv) {
                data->shader = SHADER_YUV_BT709;
            } else if (texture->format == SDL_PIXELFORMAT_NV12) {
                if (SDL_GetHintBoolean("SDL_RENDER_OPENGL_NV12_RG_SHADER", SDL_FALSE)) {
                    data->shader = SHADER_NV12_RG_BT709;
                } else {
                    data->shader = SHADER_NV12_RA_BT709;
                }
            } else {
                data->shader = SHADER_NV21_BT709;
            }
            break;
        default:
            SDL_assert(!"unsupported YUV conversion mode");
            break;
        }
    }
#endif /* SDL_HAVE_YUV */

    return GL_CheckError("", renderer);
}

#ifdef __DREAMCAST__
SDL_Texture *SDL_LoadDreamcastTexture_RW(SDL_Renderer *renderer, SDL_RWops *src, int closeio)
{
    SDL_DreamcastDtHeader header;
    SDL_Texture *texture = NULL;
    GL_RenderData *renderdata;
    GL_TextureData *data;
    Uint8 *payload = NULL;
    Uint32 header_size;
    Uint32 payload_size;
    Uint32 sdl_format;
    GLenum internal_format;
    GLenum upload_format = 0;
    GLenum upload_type = 0;
    SDL_bool texture_enabled = SDL_FALSE;
    SDL_bool compressed;
    SDL_bool mipmapped;
    SDL_bool twiddled;
    int dt_format;
    int result = -1;

    if (!renderer) {
        SDL_InvalidParamError("renderer");
        goto done;
    }
    if (!src) {
        SDL_InvalidParamError("src");
        goto done;
    }
    if (!renderer->driverdata) {
        SDL_SetError("Dreamcast .dt textures require an active OpenGL renderer");
        goto done;
    }

    if (SDL_RWread(src, &header, sizeof(header), 1) != 1) {
        SDL_SetError("Could not read Dreamcast .dt header");
        goto done;
    }

    if (SDL_memcmp(header.fourcc, "DcTx", 4) != 0) {
        SDL_SetError("Invalid Dreamcast .dt texture magic");
        goto done;
    }
    if (header.version != 0) {
        SDL_SetError("Unsupported Dreamcast .dt version %u", (unsigned)header.version);
        goto done;
    }

    header_size = ((Uint32)header.header_size + 1u) * SDL_DC_DT_HEADER_SIZE;
    if (header_size < SDL_DC_DT_HEADER_SIZE || header.chunk_size <= header_size) {
        SDL_SetError("Invalid Dreamcast .dt texture size");
        goto done;
    }
    if (header.width_pixels == 0 || header.height_pixels == 0) {
        SDL_SetError("Invalid Dreamcast .dt texture dimensions");
        goto done;
    }
    if (GL_DreamcastDtStrided(header.pvr_type)) {
        SDL_SetError("Dreamcast strided .dt textures are not supported by the SDL OpenGL renderer yet");
        goto done;
    }

    dt_format = GL_DreamcastDtPixelFormat(header.pvr_type);
    sdl_format = GL_DreamcastDtSDLFormat(dt_format);
    if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
        SDL_SetError("Unsupported Dreamcast .dt pixel format %d", dt_format);
        goto done;
    }

    compressed = GL_DreamcastDtCompressed(header.pvr_type);
    mipmapped = GL_DreamcastDtMipmapped(header.pvr_type);
    twiddled = GL_DreamcastDtTwiddled(header.pvr_type);
    internal_format = GL_DreamcastDtInternalFormat(dt_format, compressed, twiddled, mipmapped);
    if (!internal_format) {
        SDL_SetError("Unsupported Dreamcast .dt texture format");
        goto done;
    }

    if (!compressed) {
        GL_DreamcastDtFormatAndType(dt_format, twiddled, &upload_format, &upload_type);
        if (!upload_format || !upload_type) {
            SDL_SetError("Unsupported Dreamcast .dt upload type");
            goto done;
        }
    }

    payload_size = header.chunk_size - header_size;
    if (payload_size > 0x7fffffffU) {
        SDL_SetError("Dreamcast .dt texture payload is too large");
        goto done;
    }

    if (header_size > SDL_DC_DT_HEADER_SIZE) {
        if (SDL_RWseek(src, (Sint64)header_size, RW_SEEK_SET) < 0) {
            SDL_SetError("Could not seek to Dreamcast .dt texture payload");
            goto done;
        }
    }

    payload = (Uint8 *)SDL_malloc(payload_size);
    if (!payload) {
        SDL_OutOfMemory();
        goto done;
    }
    if (SDL_RWread(src, payload, 1, payload_size) != payload_size) {
        SDL_SetError("Could not read Dreamcast .dt texture payload");
        goto done;
    }

    texture = SDL_CreateTexture(renderer, sdl_format, SDL_TEXTUREACCESS_STATIC,
                                (int)header.width_pixels, (int)header.height_pixels);
    if (!texture) {
        goto done;
    }

    renderdata = (GL_RenderData *)renderer->driverdata;
    data = (GL_TextureData *)texture->driverdata;
    if (!data) {
        SDL_SetError("Dreamcast .dt texture requires OpenGL texture data");
        goto done;
    }

    GL_ActivateRenderer(renderer);
    renderdata->drawstate.texture = NULL;
    renderdata->drawstate.texturing = SDL_FALSE;
    renderdata->glEnable(renderdata->textype);
    texture_enabled = SDL_TRUE;
    renderdata->glBindTexture(renderdata->textype, data->texture);
    GL_ClearErrors(renderer);

    if (compressed) {
        glCompressedTexImage2DARB(renderdata->textype, 0, internal_format,
                                  header.width_pixels, header.height_pixels, 0,
                                  (GLsizei)payload_size, payload);
    } else {
        renderdata->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        renderdata->glTexImage2D(renderdata->textype, 0, internal_format,
                                 header.width_pixels, header.height_pixels, 0,
                                 upload_format, upload_type, payload);
        data->format = upload_format;
        data->formattype = upload_type;
    }

    if (GL_CheckError("Dreamcast .dt texture upload", renderer) < 0) {
        goto done;
    }

    SDL_Log("Dreamcast: loaded .dt texture %ux%u fmt=%d compressed=%d twiddled=%d mipmapped=%d",
            (unsigned)header.width_pixels, (unsigned)header.height_pixels, dt_format,
            (int)compressed, (int)twiddled, (int)mipmapped);
    result = 0;

done:
    if (texture_enabled) {
        renderdata->glDisable(renderdata->textype);
    }
    if (closeio && src) {
        SDL_RWclose(src);
    }
    SDL_free(payload);
    if (result < 0) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
        return NULL;
    }
    return texture;
}
#endif

static int GL_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                            const SDL_Rect *rect, const void *pixels, int pitch)
{
    GL_RenderData *renderdata = (GL_RenderData *)renderer->driverdata;
    const GLenum textype = renderdata->textype;
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;
    const int texturebpp = SDL_BYTESPERPIXEL(texture->format);
#ifdef __DREAMCAST__
    void *converted_pixels = NULL;
    const void *upload_pixels = pixels;
    int upload_pitch = pitch;
#endif

    SDL_assert_release(texturebpp != 0); /* otherwise, division by zero later. */

    GL_ActivateRenderer(renderer);

    renderdata->drawstate.texture = NULL; /* we trash this state. */

#ifdef __DREAMCAST__
    if (data->original_format != texture->format) {
        upload_pitch = rect->w * SDL_BYTESPERPIXEL(texture->format);
        converted_pixels = SDL_malloc((size_t)upload_pitch * rect->h);
        if (!converted_pixels) {
            return SDL_OutOfMemory();
        }

        SDL_Log("Dreamcast: converting upload from %s to %s",
                SDL_GetPixelFormatName(data->original_format),
                SDL_GetPixelFormatName(texture->format));

        if (SDL_ConvertPixels(rect->w,
                              rect->h,
                              data->original_format,
                              pixels,
                              pitch,
                              texture->format,
                              converted_pixels,
                              upload_pitch) < 0) {
            SDL_free(converted_pixels);
            return -1;
        }

        upload_pixels = converted_pixels;
    }
#endif

    renderdata->glBindTexture(textype, data->texture);
    
    renderdata->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef __DREAMCAST__
    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, (upload_pitch / texturebpp));
#else
    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, (pitch / texturebpp));
#endif
SDL_Log("GL_UpdateTexture: format=0x%X type=0x%X original_format=%s texture_format=%s upload_pitch=%d w=%d h=%d conversion_fired=%s",
        data->format,
        data->formattype,
        SDL_GetPixelFormatName(data->original_format),
        SDL_GetPixelFormatName(texture->format),
        upload_pitch,
        rect->w,
        rect->h,
        (data->original_format != texture->format) ? "YES" : "NO");
        
    renderdata->glTexSubImage2D(textype, 0, rect->x, rect->y, rect->w,
                                rect->h, data->format, data->formattype,
#ifdef __DREAMCAST__
                                upload_pixels);
    SDL_free(converted_pixels);
#else
                                pixels);
#endif

#if SDL_HAVE_YUV
    if (data->yuv) {
        renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, ((pitch + 1) / 2));

        /* Skip to the correct offset into the next texture */
        pixels = (const void *)((const Uint8 *)pixels + rect->h * pitch);
        if (texture->format == SDL_PIXELFORMAT_YV12) {
            renderdata->glBindTexture(textype, data->vtexture);
        } else {
            renderdata->glBindTexture(textype, data->utexture);
        }
        renderdata->glTexSubImage2D(textype, 0, rect->x / 2, rect->y / 2,
                                    (rect->w + 1) / 2, (rect->h + 1) / 2,
                                    data->format, data->formattype, pixels);

        /* Skip to the correct offset into the next texture */
        pixels = (const void *)((const Uint8 *)pixels + ((rect->h + 1) / 2) * ((pitch + 1) / 2));
        if (texture->format == SDL_PIXELFORMAT_YV12) {
            renderdata->glBindTexture(textype, data->utexture);
        } else {
            renderdata->glBindTexture(textype, data->vtexture);
        }
        renderdata->glTexSubImage2D(textype, 0, rect->x / 2, rect->y / 2,
                                    (rect->w + 1) / 2, (rect->h + 1) / 2,
                                    data->format, data->formattype, pixels);
    }

    if (data->nv12) {
        renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, ((pitch + 1) / 2));

        /* Skip to the correct offset into the next texture */
        pixels = (const void *)((const Uint8 *)pixels + rect->h * pitch);
        renderdata->glBindTexture(textype, data->utexture);
        renderdata->glTexSubImage2D(textype, 0, rect->x / 2, rect->y / 2,
                                    (rect->w + 1) / 2, (rect->h + 1) / 2,
                                    GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, pixels);
    }
#endif
    return GL_CheckError("glTexSubImage2D()", renderer);
}

#if SDL_HAVE_YUV
static int GL_UpdateTextureYUV(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_Rect *rect,
                               const Uint8 *Yplane, int Ypitch,
                               const Uint8 *Uplane, int Upitch,
                               const Uint8 *Vplane, int Vpitch)
{
    GL_RenderData *renderdata = (GL_RenderData *)renderer->driverdata;
    const GLenum textype = renderdata->textype;
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;

    GL_ActivateRenderer(renderer);

    renderdata->drawstate.texture = NULL; /* we trash this state. */

    renderdata->glBindTexture(textype, data->texture);
    renderdata->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, Ypitch);
    renderdata->glTexSubImage2D(textype, 0, rect->x, rect->y, rect->w,
                                rect->h, data->format, data->formattype,
                                Yplane);

    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, Upitch);
    renderdata->glBindTexture(textype, data->utexture);
    renderdata->glTexSubImage2D(textype, 0, rect->x / 2, rect->y / 2,
                                (rect->w + 1) / 2, (rect->h + 1) / 2,
                                data->format, data->formattype, Uplane);

    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, Vpitch);
    renderdata->glBindTexture(textype, data->vtexture);
    renderdata->glTexSubImage2D(textype, 0, rect->x / 2, rect->y / 2,
                                (rect->w + 1) / 2, (rect->h + 1) / 2,
                                data->format, data->formattype, Vplane);

    return GL_CheckError("glTexSubImage2D()", renderer);
}

static int GL_UpdateTextureNV(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *rect,
                              const Uint8 *Yplane, int Ypitch,
                              const Uint8 *UVplane, int UVpitch)
{
    GL_RenderData *renderdata = (GL_RenderData *)renderer->driverdata;
    const GLenum textype = renderdata->textype;
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;

    GL_ActivateRenderer(renderer);

    renderdata->drawstate.texture = NULL; /* we trash this state. */

    renderdata->glBindTexture(textype, data->texture);
    renderdata->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, Ypitch);
    renderdata->glTexSubImage2D(textype, 0, rect->x, rect->y, rect->w,
                                rect->h, data->format, data->formattype,
                                Yplane);

    renderdata->glPixelStorei(GL_UNPACK_ROW_LENGTH, UVpitch / 2);
    renderdata->glBindTexture(textype, data->utexture);
    renderdata->glTexSubImage2D(textype, 0, rect->x / 2, rect->y / 2,
                                (rect->w + 1) / 2, (rect->h + 1) / 2,
                                GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, UVplane);

    return GL_CheckError("glTexSubImage2D()", renderer);
}
#endif

static int GL_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                          const SDL_Rect *rect, void **pixels, int *pitch)
{
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;

    data->locked_rect = *rect;
    *pixels =
        (void *)((Uint8 *)data->pixels + rect->y * data->pitch +
                 rect->x * SDL_BYTESPERPIXEL(texture->format));
    *pitch = data->pitch;
    return 0;
}

static void GL_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;
    const SDL_Rect *rect;
    void *pixels;

    rect = &data->locked_rect;
    pixels =
        (void *)((Uint8 *)data->pixels + rect->y * data->pitch +
                 rect->x * SDL_BYTESPERPIXEL(texture->format));
    GL_UpdateTexture(renderer, texture, rect, pixels, data->pitch);
}

static void GL_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    GL_RenderData *renderdata = (GL_RenderData *)renderer->driverdata;
    const GLenum textype = renderdata->textype;
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;
    GLenum glScaleMode = (scaleMode == SDL_ScaleModeNearest) ? GL_NEAREST : GL_LINEAR;

    renderdata->glBindTexture(textype, data->texture);
    renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER, glScaleMode);
    renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER, glScaleMode);

#if SDL_HAVE_YUV
    if (texture->format == SDL_PIXELFORMAT_YV12 ||
        texture->format == SDL_PIXELFORMAT_IYUV) {
        renderdata->glBindTexture(textype, data->utexture);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER, glScaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER, glScaleMode);

        renderdata->glBindTexture(textype, data->vtexture);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER, glScaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER, glScaleMode);
    }

    if (texture->format == SDL_PIXELFORMAT_NV12 ||
        texture->format == SDL_PIXELFORMAT_NV21) {
        renderdata->glBindTexture(textype, data->utexture);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MIN_FILTER, glScaleMode);
        renderdata->glTexParameteri(textype, GL_TEXTURE_MAG_FILTER, glScaleMode);
    }
#endif
}

static int GL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;
    GL_TextureData *texturedata;
    GLenum status;

    GL_ActivateRenderer(renderer);

    if (!data->GL_EXT_framebuffer_object_supported) {
        return SDL_SetError("Render targets not supported by OpenGL");
    }

    data->drawstate.viewport_dirty = SDL_TRUE;

    if (!texture) {
        data->glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        return 0;
    }

    texturedata = (GL_TextureData *)texture->driverdata;
    data->glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, texturedata->fbo->FBO);
    /* TODO: check if texture pixel format allows this operation */
    data->glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, data->textype, texturedata->texture, 0);
    /* Check FBO status */
    status = data->glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        return SDL_SetError("glFramebufferTexture2DEXT() failed");
    }
    return 0;
}

/* !!! FIXME: all these Queue* calls set up the vertex buffer the way the immediate mode
   !!! FIXME:  renderer wants it, but this might want to operate differently if we move to
   !!! FIXME:  VBOs at some point. */
static int GL_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return 0; /* nothing to do in this backend. */
}

static int GL_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    GLfloat *verts = (GLfloat *)SDL_AllocateRenderVertices(renderer, count * 2 * sizeof(GLfloat), 0, &cmd->data.draw.first);
    int i;

    if (!verts) {
        return -1;
    }

    cmd->data.draw.count = count;
    for (i = 0; i < count; i++) {
        *(verts++) = 0.5f + points[i].x;
        *(verts++) = 0.5f + points[i].y;
    }

    return 0;
}

static int GL_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    int i;
    GLfloat prevx, prevy;
    const size_t vertlen = (sizeof(GLfloat) * 2) * count;
    GLfloat *verts = (GLfloat *)SDL_AllocateRenderVertices(renderer, vertlen, 0, &cmd->data.draw.first);

    if (!verts) {
        return -1;
    }
    cmd->data.draw.count = count;

    /* 0.5f offset to hit the center of the pixel. */
    prevx = 0.5f + points->x;
    prevy = 0.5f + points->y;
    *(verts++) = prevx;
    *(verts++) = prevy;

    /* bump the end of each line segment out a quarter of a pixel, to provoke
       the diamond-exit rule. Without this, you won't just drop the last
       pixel of the last line segment, but you might also drop pixels at the
       edge of any given line segment along the way too. */
    for (i = 1; i < count; i++) {
        const GLfloat xstart = prevx;
        const GLfloat ystart = prevy;
        const GLfloat xend = points[i].x + 0.5f; /* 0.5f to hit pixel center. */
        const GLfloat yend = points[i].y + 0.5f;
        /* bump a little in the direction we are moving in. */
        const GLfloat deltax = xend - xstart;
        const GLfloat deltay = yend - ystart;
        const GLfloat angle = SDL_atan2f(deltay, deltax);
        prevx = xend + (SDL_cosf(angle) * 0.25f);
        prevy = yend + (SDL_sinf(angle) * 0.25f);
        *(verts++) = prevx;
        *(verts++) = prevy;
    }

    return 0;
}

static int GL_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                            const float *xy, int xy_stride, const SDL_Color *color, int color_stride, const float *uv, int uv_stride,
                            int num_vertices, const void *indices, int num_indices, int size_indices,
                            float scale_x, float scale_y)
{
    GL_TextureData *texturedata = NULL;
    int i;
    int count = indices ? num_indices : num_vertices;
    GLfloat *verts;
    size_t sz = 2 * sizeof(GLfloat) + 4 * sizeof(Uint8) + (texture ? 2 : 0) * sizeof(GLfloat);

    verts = (GLfloat *)SDL_AllocateRenderVertices(renderer, count * sz, 0, &cmd->data.draw.first);
    if (!verts) {
        return -1;
    }

    if (texture) {
        texturedata = (GL_TextureData *)texture->driverdata;
    }

    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;

    for (i = 0; i < count; i++) {
        int j;
        float *xy_;
        if (size_indices == 4) {
            j = ((const Uint32 *)indices)[i];
        } else if (size_indices == 2) {
            j = ((const Uint16 *)indices)[i];
        } else if (size_indices == 1) {
            j = ((const Uint8 *)indices)[i];
        } else {
            j = i;
        }

        xy_ = (float *)((char *)xy + j * xy_stride);

        *(verts++) = xy_[0] * scale_x;
        *(verts++) = xy_[1] * scale_y;

        /* Not really a float, but it is still 4 bytes and will be cast to the
           right type in the graphics driver. */
        SDL_memcpy(verts, ((char *)color + j * color_stride), sizeof(*color));
        ++verts;

        if (texture) {
            float *uv_ = (float *)((char *)uv + j * uv_stride);
            *(verts++) = uv_[0] * texturedata->texw;
            *(verts++) = uv_[1] * texturedata->texh;
        }
    }
    return 0;
}

static int SetDrawState(GL_RenderData *data, const SDL_RenderCommand *cmd, const GL_Shader shader)
{
    const SDL_BlendMode blend = cmd->data.draw.blend;
    SDL_bool vertex_array;
    SDL_bool color_array;
    SDL_bool texture_array;

    if (data->drawstate.viewport_dirty) {
        const SDL_bool istarget = data->drawstate.target != NULL;
        const SDL_Rect *viewport = &data->drawstate.viewport;
        data->glMatrixMode(GL_PROJECTION);
        data->glLoadIdentity();
        data->glViewport(viewport->x,
                         istarget ? viewport->y : (data->drawstate.drawableh - viewport->y - viewport->h),
                         viewport->w, viewport->h);
        if (viewport->w && viewport->h) {
            data->glOrtho((GLdouble)0, (GLdouble)viewport->w,
                          (GLdouble)istarget ? 0 : viewport->h,
                          (GLdouble)istarget ? viewport->h : 0,
                          0.0, 1.0);
        }
        data->glMatrixMode(GL_MODELVIEW);
        data->drawstate.viewport_dirty = SDL_FALSE;
    }

    if (data->drawstate.cliprect_enabled_dirty) {
        if (!data->drawstate.cliprect_enabled) {
            data->glDisable(GL_SCISSOR_TEST);
        } else {
            data->glEnable(GL_SCISSOR_TEST);
        }
        data->drawstate.cliprect_enabled_dirty = SDL_FALSE;
    }

    if (data->drawstate.cliprect_enabled && data->drawstate.cliprect_dirty) {
        const SDL_Rect *viewport = &data->drawstate.viewport;
        const SDL_Rect *rect = &data->drawstate.cliprect;
        data->glScissor(viewport->x + rect->x,
                        data->drawstate.target ? viewport->y + rect->y : data->drawstate.drawableh - viewport->y - rect->y - rect->h,
                        rect->w, rect->h);
        data->drawstate.cliprect_dirty = SDL_FALSE;
    }

    if (blend != data->drawstate.blend) {
        if (blend == SDL_BLENDMODE_NONE) {
            data->glDisable(GL_BLEND);
        } else {
            data->glEnable(GL_BLEND);
            data->glBlendFuncSeparate(GetBlendFunc(SDL_GetBlendModeSrcColorFactor(blend)),
                                      GetBlendFunc(SDL_GetBlendModeDstColorFactor(blend)),
                                      GetBlendFunc(SDL_GetBlendModeSrcAlphaFactor(blend)),
                                      GetBlendFunc(SDL_GetBlendModeDstAlphaFactor(blend)));
            data->glBlendEquation(GetBlendEquation(SDL_GetBlendModeColorOperation(blend)));
        }
        data->drawstate.blend = blend;
    }

    if (data->shaders && (shader != data->drawstate.shader)) {
        GL_SelectShader(data->shaders, shader);
        data->drawstate.shader = shader;
    }

    if ((cmd->data.draw.texture != NULL) != data->drawstate.texturing) {
        if (!cmd->data.draw.texture) {
            data->glDisable(data->textype);
            data->drawstate.texturing = SDL_FALSE;
        } else {
            data->glEnable(data->textype);
            data->drawstate.texturing = SDL_TRUE;
        }
    }

    vertex_array = cmd->command == SDL_RENDERCMD_DRAW_POINTS || cmd->command == SDL_RENDERCMD_DRAW_LINES || cmd->command == SDL_RENDERCMD_GEOMETRY;
    color_array = cmd->command == SDL_RENDERCMD_GEOMETRY;
    texture_array = cmd->data.draw.texture != NULL;

    if (vertex_array != data->drawstate.vertex_array) {
        if (vertex_array) {
            data->glEnableClientState(GL_VERTEX_ARRAY);
        } else {
            data->glDisableClientState(GL_VERTEX_ARRAY);
        }
        data->drawstate.vertex_array = vertex_array;
    }

    if (color_array != data->drawstate.color_array) {
        if (color_array) {
            data->glEnableClientState(GL_COLOR_ARRAY);
        } else {
            data->glDisableClientState(GL_COLOR_ARRAY);
        }
        data->drawstate.color_array = color_array;
    }

    /* This is a little awkward but should avoid texcoord arrays getting into
       a bad state if SDL_GL_BindTexture/UnbindTexture are called. */
    if (texture_array != data->drawstate.texture_array) {
        if (texture_array) {
            data->glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        } else {
            data->glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        data->drawstate.texture_array = texture_array;
    }

    return 0;
}

static int SetCopyState(GL_RenderData *data, const SDL_RenderCommand *cmd)
{
    SDL_Texture *texture = cmd->data.draw.texture;
    const GL_TextureData *texturedata = (GL_TextureData *)texture->driverdata;

    SetDrawState(data, cmd, texturedata->shader);

    if (texture != data->drawstate.texture) {
        const GLenum textype = data->textype;
#if SDL_HAVE_YUV
        if (texturedata->yuv) {
            if (data->GL_ARB_multitexture_supported) {
                data->glActiveTextureARB(GL_TEXTURE2_ARB);
            }
            data->glBindTexture(textype, texturedata->vtexture);

            if (data->GL_ARB_multitexture_supported) {
                data->glActiveTextureARB(GL_TEXTURE1_ARB);
            }
            data->glBindTexture(textype, texturedata->utexture);
        }
        if (texturedata->nv12) {
            if (data->GL_ARB_multitexture_supported) {
                data->glActiveTextureARB(GL_TEXTURE1_ARB);
            }
            data->glBindTexture(textype, texturedata->utexture);
        }
#endif
        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE0_ARB);
        }
        data->glBindTexture(textype, texturedata->texture);

        data->drawstate.texture = texture;
    }

    return 0;
}

static int GL_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    /* !!! FIXME: it'd be nice to use a vertex buffer instead of immediate mode... */
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;

    if (GL_ActivateRenderer(renderer) < 0) {
        return -1;
    }

    data->drawstate.target = renderer->target;
    if (!data->drawstate.target) {
        int w, h;
        SDL_GL_GetDrawableSize(renderer->window, &w, &h);
        if ((w != data->drawstate.drawablew) || (h != data->drawstate.drawableh)) {
            data->drawstate.viewport_dirty = SDL_TRUE; // if the window dimensions changed, invalidate the current viewport, etc.
            data->drawstate.cliprect_dirty = SDL_TRUE;
            data->drawstate.drawablew = w;
            data->drawstate.drawableh = h;
        }
    }

#ifdef __MACOSX__
    // On macOS on older systems, the OpenGL view change and resize events aren't
    // necessarily synchronized, so just always reset it.
    // Workaround for: https://discourse.libsdl.org/t/sdl-2-0-22-prerelease/35306/6
    data->drawstate.viewport_dirty = SDL_TRUE;
#endif

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETDRAWCOLOR:
        {
            const Uint8 r = cmd->data.color.r;
            const Uint8 g = cmd->data.color.g;
            const Uint8 b = cmd->data.color.b;
            const Uint8 a = cmd->data.color.a;
            const Uint32 color = (((Uint32)a << 24) | (r << 16) | (g << 8) | b);
            if (color != data->drawstate.color) {
                data->glColor4ub((GLubyte)r, (GLubyte)g, (GLubyte)b, (GLubyte)a);
                data->drawstate.color = color;
            }
            break;
        }

        case SDL_RENDERCMD_SETVIEWPORT:
        {
            SDL_Rect *viewport = &data->drawstate.viewport;
            if (SDL_memcmp(viewport, &cmd->data.viewport.rect, sizeof(cmd->data.viewport.rect)) != 0) {
                SDL_copyp(viewport, &cmd->data.viewport.rect);
                data->drawstate.viewport_dirty = SDL_TRUE;
                data->drawstate.cliprect_dirty = SDL_TRUE;
            }
            break;
        }

        case SDL_RENDERCMD_SETCLIPRECT:
        {
            const SDL_Rect *rect = &cmd->data.cliprect.rect;
            if (data->drawstate.cliprect_enabled != cmd->data.cliprect.enabled) {
                data->drawstate.cliprect_enabled = cmd->data.cliprect.enabled;
                data->drawstate.cliprect_enabled_dirty = SDL_TRUE;
            }

            if (SDL_memcmp(&data->drawstate.cliprect, rect, sizeof(*rect)) != 0) {
                SDL_copyp(&data->drawstate.cliprect, rect);
                data->drawstate.cliprect_dirty = SDL_TRUE;
            }
            break;
        }

        case SDL_RENDERCMD_CLEAR:
        {
            const Uint8 r = cmd->data.color.r;
            const Uint8 g = cmd->data.color.g;
            const Uint8 b = cmd->data.color.b;
            const Uint8 a = cmd->data.color.a;
            const Uint32 color = (((Uint32)a << 24) | (r << 16) | (g << 8) | b);
            if (color != data->drawstate.clear_color) {
                const GLfloat fr = ((GLfloat)r) * inv255f;
                const GLfloat fg = ((GLfloat)g) * inv255f;
                const GLfloat fb = ((GLfloat)b) * inv255f;
                const GLfloat fa = ((GLfloat)a) * inv255f;
                data->glClearColor(fr, fg, fb, fa);
                data->drawstate.clear_color = color;
            }

            if (data->drawstate.cliprect_enabled || data->drawstate.cliprect_enabled_dirty) {
                data->glDisable(GL_SCISSOR_TEST);
                data->drawstate.cliprect_enabled_dirty = data->drawstate.cliprect_enabled;
            }

            data->glClear(GL_COLOR_BUFFER_BIT);
            break;
        }

        case SDL_RENDERCMD_FILL_RECTS: /* unused */
            break;

        case SDL_RENDERCMD_COPY: /* unused */
            break;

        case SDL_RENDERCMD_COPY_EX: /* unused */
            break;

        case SDL_RENDERCMD_DRAW_LINES:
        {
            if (SetDrawState(data, cmd, SHADER_SOLID) == 0) {
                size_t count = cmd->data.draw.count;
                const GLfloat *verts = (GLfloat *)(((Uint8 *)vertices) + cmd->data.draw.first);

                /* SetDrawState handles glEnableClientState. */
                data->glVertexPointer(2, GL_FLOAT, sizeof(float) * 2, verts);

                if (count > 2) {
                    /* joined lines cannot be grouped */
                    data->glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)count);
                } else {
                    /* let's group non joined lines */
                    SDL_RenderCommand *finalcmd = cmd;
                    SDL_RenderCommand *nextcmd = cmd->next;
                    SDL_BlendMode thisblend = cmd->data.draw.blend;

                    while (nextcmd) {
                        const SDL_RenderCommandType nextcmdtype = nextcmd->command;
                        if (nextcmdtype != SDL_RENDERCMD_DRAW_LINES) {
                            break; /* can't go any further on this draw call, different render command up next. */
                        } else if (nextcmd->data.draw.count != 2) {
                            break; /* can't go any further on this draw call, those are joined lines */
                        } else if (nextcmd->data.draw.blend != thisblend) {
                            break; /* can't go any further on this draw call, different blendmode copy up next. */
                        } else {
                            finalcmd = nextcmd; /* we can combine copy operations here. Mark this one as the furthest okay command. */
                            count += nextcmd->data.draw.count;
                        }
                        nextcmd = nextcmd->next;
                    }

                    data->glDrawArrays(GL_LINES, 0, (GLsizei)count);
                    cmd = finalcmd; /* skip any copy commands we just combined in here. */
                }
            }
            break;
        }

        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_GEOMETRY:
        {
            /* as long as we have the same copy command in a row, with the
               same texture, we can combine them all into a single draw call. */
            SDL_Texture *thistexture = cmd->data.draw.texture;
            SDL_BlendMode thisblend = cmd->data.draw.blend;
            const SDL_RenderCommandType thiscmdtype = cmd->command;
            SDL_RenderCommand *finalcmd = cmd;
            SDL_RenderCommand *nextcmd = cmd->next;
            size_t count = cmd->data.draw.count;
            int ret;
            while (nextcmd) {
                const SDL_RenderCommandType nextcmdtype = nextcmd->command;
                if (nextcmdtype != thiscmdtype) {
                    break; /* can't go any further on this draw call, different render command up next. */
                } else if (nextcmd->data.draw.texture != thistexture || nextcmd->data.draw.blend != thisblend) {
                    break; /* can't go any further on this draw call, different texture/blendmode copy up next. */
                } else {
                    finalcmd = nextcmd; /* we can combine copy operations here. Mark this one as the furthest okay command. */
                    count += nextcmd->data.draw.count;
                }
                nextcmd = nextcmd->next;
            }

            if (thistexture) {
                ret = SetCopyState(data, cmd);
            } else {
                ret = SetDrawState(data, cmd, SHADER_SOLID);
            }

            if (ret == 0) {
                const GLfloat *verts = (GLfloat *)(((Uint8 *)vertices) + cmd->data.draw.first);
                int op = GL_TRIANGLES; /* SDL_RENDERCMD_GEOMETRY */
                if (thiscmdtype == SDL_RENDERCMD_DRAW_POINTS) {
                    op = GL_POINTS;
                }

                if (thiscmdtype == SDL_RENDERCMD_DRAW_POINTS) {
                    /* SetDrawState handles glEnableClientState. */
                    data->glVertexPointer(2, GL_FLOAT, sizeof(float) * 2, verts);
                } else {
                    /* SetDrawState handles glEnableClientState. */
                    if (thistexture) {
                        data->glVertexPointer(2, GL_FLOAT, sizeof(float) * 5, verts + 0);
                        data->glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(float) * 5, verts + 2);
                        data->glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 5, verts + 3);
                    } else {
                        data->glVertexPointer(2, GL_FLOAT, sizeof(float) * 3, verts + 0);
                        data->glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(float) * 3, verts + 2);
                    }
                }

                data->glDrawArrays(op, 0, (GLsizei)count);

                /* Restore previously set color when we're done. */
                if (thiscmdtype != SDL_RENDERCMD_DRAW_POINTS) {
                    Uint32 color = data->drawstate.color;
                    GLubyte a = (GLubyte)((color >> 24) & 0xFF);
                    GLubyte r = (GLubyte)((color >> 16) & 0xFF);
                    GLubyte g = (GLubyte)((color >> 8) & 0xFF);
                    GLubyte b = (GLubyte)((color >> 0) & 0xFF);
                    data->glColor4ub(r, g, b, a);
                }
            }

            cmd = finalcmd; /* skip any copy commands we just combined in here. */
            break;
        }

        case SDL_RENDERCMD_NO_OP:
            break;
        }

        cmd = cmd->next;
    }

    /* Turn off vertex array state when we're done, in case external code
       relies on it being off. */
    if (data->drawstate.vertex_array) {
        data->glDisableClientState(GL_VERTEX_ARRAY);
        data->drawstate.vertex_array = SDL_FALSE;
    }
    if (data->drawstate.color_array) {
        data->glDisableClientState(GL_COLOR_ARRAY);
        data->drawstate.color_array = SDL_FALSE;
    }
    if (data->drawstate.texture_array) {
        data->glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        data->drawstate.texture_array = SDL_FALSE;
    }

    return GL_CheckError("", renderer);
}

static int GL_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                               Uint32 pixel_format, void *pixels, int pitch)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;
    Uint32 temp_format = renderer->target ? renderer->target->format : SDL_PIXELFORMAT_ARGB8888;
    void *temp_pixels;
    int temp_pitch;
    GLint internalFormat;
    GLenum format, type;
    Uint8 *src, *dst, *tmp;
    int w, h, length, rows;
    int status;

    GL_ActivateRenderer(renderer);

    if (!convert_format(data, temp_format, &internalFormat, &format, &type)) {
        return SDL_SetError("Texture format %s not supported by OpenGL",
                            SDL_GetPixelFormatName(temp_format));
    }

    if (rect->w == 0 || rect->h == 0) {
        return 0; /* nothing to do. */
    }

    temp_pitch = rect->w * SDL_BYTESPERPIXEL(temp_format);
    temp_pixels = SDL_malloc((size_t)rect->h * temp_pitch);
    if (!temp_pixels) {
        return SDL_OutOfMemory();
    }

    SDL_GetRendererOutputSize(renderer, &w, &h);

    data->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    data->glPixelStorei(GL_PACK_ROW_LENGTH,
                        (temp_pitch / SDL_BYTESPERPIXEL(temp_format)));

    data->glReadPixels(rect->x, renderer->target ? rect->y : (h - rect->y) - rect->h,
                       rect->w, rect->h, format, type, temp_pixels);

    if (GL_CheckError("glReadPixels()", renderer) < 0) {
        SDL_free(temp_pixels);
        return -1;
    }

    /* Flip the rows to be top-down if necessary */
    if (!renderer->target) {
        SDL_bool isstack;
        length = rect->w * SDL_BYTESPERPIXEL(temp_format);
        src = (Uint8 *)temp_pixels + (rect->h - 1) * temp_pitch;
        dst = (Uint8 *)temp_pixels;
        tmp = SDL_small_alloc(Uint8, length, &isstack);
        rows = rect->h / 2;
        while (rows--) {
            SDL_memcpy(tmp, dst, length);
            SDL_memcpy(dst, src, length);
            SDL_memcpy(src, tmp, length);
            dst += temp_pitch;
            src -= temp_pitch;
        }
        SDL_small_free(tmp, isstack);
    }

    status = SDL_ConvertPixels(rect->w, rect->h,
                               temp_format, temp_pixels, temp_pitch,
                               pixel_format, pixels, pitch);
    SDL_free(temp_pixels);

    return status;
}

static int GL_RenderPresent(SDL_Renderer *renderer)
{
    GL_ActivateRenderer(renderer);

    return SDL_GL_SwapWindowWithResult(renderer->window);
}

static void GL_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GL_RenderData *renderdata = (GL_RenderData *)renderer->driverdata;
    GL_TextureData *data = (GL_TextureData *)texture->driverdata;

    GL_ActivateRenderer(renderer);

    if (renderdata->drawstate.texture == texture) {
        renderdata->drawstate.texture = NULL;
    }
    if (renderdata->drawstate.target == texture) {
        renderdata->drawstate.target = NULL;
    }

    if (!data) {
        return;
    }
    if (data->texture) {
        renderdata->glDeleteTextures(1, &data->texture);
    }
#if SDL_HAVE_YUV
    if (data->yuv) {
        renderdata->glDeleteTextures(1, &data->utexture);
        renderdata->glDeleteTextures(1, &data->vtexture);
    }
#endif
    SDL_free(data->pixels);
    SDL_free(data);
    texture->driverdata = NULL;
}

static void GL_DestroyRenderer(SDL_Renderer *renderer)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;

    if (data) {
        if (data->context) {
            /* make sure we delete the right resources! */
            GL_ActivateRenderer(renderer);
        }

        GL_ClearErrors(renderer);
        if (data->GL_ARB_debug_output_supported) {
            PFNGLDEBUGMESSAGECALLBACKARBPROC glDebugMessageCallbackARBFunc = (PFNGLDEBUGMESSAGECALLBACKARBPROC)SDL_GL_GetProcAddress("glDebugMessageCallbackARB");

            /* Uh oh, we don't have a safe way of removing ourselves from the callback chain, if it changed after we set our callback. */
            /* For now, just always replace the callback with the original one */
            glDebugMessageCallbackARBFunc(data->next_error_callback, data->next_error_userparam);
        }
        if (data->shaders) {
            GL_DestroyShaderContext(data->shaders);
        }
        if (data->context) {
            while (data->framebuffers) {
                GL_FBOList *nextnode = data->framebuffers->next;
                /* delete the framebuffer object */
                data->glDeleteFramebuffersEXT(1, &data->framebuffers->FBO);
                GL_CheckError("", renderer);
                SDL_free(data->framebuffers);
                data->framebuffers = nextnode;
            }
            SDL_GL_DeleteContext(data->context);
        }
        SDL_free(data);
    }
}

static int GL_BindTexture(SDL_Renderer *renderer, SDL_Texture *texture, float *texw, float *texh)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;
    GL_TextureData *texturedata = (GL_TextureData *)texture->driverdata;
    const GLenum textype = data->textype;

    GL_ActivateRenderer(renderer);

    data->glEnable(textype);
#if SDL_HAVE_YUV
    if (texturedata->yuv) {
        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE2_ARB);
        }
        data->glBindTexture(textype, texturedata->vtexture);

        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE1_ARB);
        }
        data->glBindTexture(textype, texturedata->utexture);

        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE0_ARB);
        }
    }
    if (texturedata->nv12) {
        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE1_ARB);
        }
        data->glBindTexture(textype, texturedata->utexture);

        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE0_ARB);
        }
    }
#endif
    data->glBindTexture(textype, texturedata->texture);

    data->drawstate.texturing = SDL_TRUE;
    data->drawstate.texture = texture;

    if (texw) {
        *texw = (float)texturedata->texw;
    }
    if (texh) {
        *texh = (float)texturedata->texh;
    }
    return 0;
}

static int GL_UnbindTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    GL_RenderData *data = (GL_RenderData *)renderer->driverdata;
    GL_TextureData *texturedata = (GL_TextureData *)texture->driverdata;
    const GLenum textype = data->textype;

    GL_ActivateRenderer(renderer);

#if SDL_HAVE_YUV
    if (texturedata->yuv) {
        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE2_ARB);
        }
        data->glBindTexture(textype, 0);
        data->glDisable(textype);

        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE1_ARB);
        }
        data->glBindTexture(textype, 0);
        data->glDisable(textype);

        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE0_ARB);
        }
    }
    if (texturedata->nv12) {
        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE1_ARB);
        }
        data->glBindTexture(textype, 0);
        data->glDisable(textype);

        if (data->GL_ARB_multitexture_supported) {
            data->glActiveTextureARB(GL_TEXTURE0_ARB);
        }
    }
#endif
    data->glBindTexture(textype, 0);
    data->glDisable(textype);

    data->drawstate.texturing = SDL_FALSE;
    data->drawstate.texture = NULL;

    return 0;
}

static int GL_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    int retval;
    if (vsync) {
        retval = SDL_GL_SetSwapInterval(1);
    } else {
        retval = SDL_GL_SetSwapInterval(0);
    }
    if (retval != 0) {
        return retval;
    }
    if (SDL_GL_GetSwapInterval() != 0) {
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    } else {
        renderer->info.flags &= ~SDL_RENDERER_PRESENTVSYNC;
    }
    return retval;
}

static SDL_bool GL_IsProbablyAccelerated(const GL_RenderData *data)
{
    /*const char *vendor = (const char *) data->glGetString(GL_VENDOR);*/
    const char *renderer = (const char *)data->glGetString(GL_RENDERER);

#if defined(__WINDOWS__) || defined(__WINGDK__)
    if (SDL_strcmp(renderer, "GDI Generic") == 0) {
        return SDL_FALSE; /* Microsoft's fallback software renderer. Fix your system! */
    }
#endif

#ifdef __APPLE__
    if (SDL_strcmp(renderer, "Apple Software Renderer") == 0) {
        return SDL_FALSE; /* (a probably very old) Apple software-based OpenGL. */
    }
#endif

    if (SDL_strcmp(renderer, "Software Rasterizer") == 0) {
        return SDL_FALSE; /* (a probably very old) Software Mesa, or some other generic thing. */
    }

    /* !!! FIXME: swrast? llvmpipe? softpipe? */

    return SDL_TRUE;
}

static int GL_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
    GL_RenderData *data;
    GLint value;
    Uint32 window_flags;
    int profile_mask = 0, major = 0, minor = 0;
    SDL_bool changed_window = SDL_FALSE;
    const char *hint;
    SDL_bool non_power_of_two_supported = SDL_FALSE;

    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile_mask);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);

#ifdef SDL_VIDEO_DRIVER_DREAMCAST  // Only log for Dreamcast
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Dreamcast renderer initialization started.");
#endif

// #if defined(SDL_VIDEO_DRIVER_DREAMCAST)  // Run this block only for Dreamcast

//     window_flags = SDL_GetWindowFlags(window);

//     // Log if Dreamcast-specific code is being executed
//     SDL_Log("SDL2 Dreamcast: Checking window flags and OpenGL context.");

//     // Proceed only if the window isn't already an OpenGL window, or the context version is incorrect
//     if (!(window_flags & SDL_WINDOW_OPENGL) || major != RENDERER_CONTEXT_MAJOR || minor != RENDERER_CONTEXT_MINOR) {

//         changed_window = SDL_TRUE;

//         // Log the context settings for Dreamcast
//         SDL_Log("SDL2 Dreamcast: Setting OpenGL context attributes - major: %d, minor: %d", RENDERER_CONTEXT_MAJOR, RENDERER_CONTEXT_MINOR);

//         // Set OpenGL context attributes for Dreamcast (no Vulkan/Metal logic here)
//         SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);  // No need for profile check on Dreamcast
//         SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, RENDERER_CONTEXT_MAJOR);
//         SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, RENDERER_CONTEXT_MINOR);

//         // Log before recreating the window
//         SDL_Log("SDL2 Dreamcast: Recreating window with OpenGL context.");

//         // Recreate the window with OpenGL for Dreamcast
//         if (SDL_RecreateWindow(window, window_flags | SDL_WINDOW_OPENGL) < 0) {
//             SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "SDL2 Dreamcast: Failed to recreate window with OpenGL context.");
//             goto error;  // Handle window creation failure
//         }

//         // Log if everything goes fine
//         SDL_Log("SDL2 Dreamcast: OpenGL window recreated successfully.");

//     } else {
//         SDL_Log("SDL2 Dreamcast: Window already has correct OpenGL context, skipping recreation.");
//     }

#if !defined(SDL_VIDEO_VITA_PVR_OGL)  // Exclude VITA-related code for Dreamcast

    // Ensure this block is excluded for VITA PVR OpenGL
    window_flags = SDL_GetWindowFlags(window);

    // Log if VITA-related code is being skipped for Dreamcast
    SDL_Log("SDL2: Skipping Vulkan/Metal logic for Dreamcast.");

    // Proceed only if the window isn't already an OpenGL window, or the context version is incorrect
    if (!(window_flags & SDL_WINDOW_OPENGL) || major != RENDERER_CONTEXT_MAJOR || minor != RENDERER_CONTEXT_MINOR) {

        changed_window = SDL_TRUE;

        // Log the context settings
        SDL_Log("SDL2: Setting OpenGL context attributes for major: %d, minor: %d", RENDERER_CONTEXT_MAJOR, RENDERER_CONTEXT_MINOR);

        // Set OpenGL context attributes
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);  // No need for profile check for Dreamcast
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, RENDERER_CONTEXT_MAJOR);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, RENDERER_CONTEXT_MINOR);

        // Log before recreating the window
        SDL_Log("SDL2: Recreating window with OpenGL context.");

        // Recreate the window with OpenGL
        if (SDL_RecreateWindow(window, window_flags | SDL_WINDOW_OPENGL) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "SDL2: Failed to recreate window with OpenGL context.");
            goto error;  // Handle window creation failure
        }

        // Log if everything goes fine
        SDL_Log("SDL2: OpenGL window recreated successfully.");

    } else {
        SDL_Log("SDL2: Window already has correct OpenGL context, skipping recreation.");
    }

#endif



    data = (GL_RenderData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        SDL_OutOfMemory();
        goto error;
    }

    renderer->WindowEvent = GL_WindowEvent;
    renderer->GetOutputSize = GL_GetOutputSize;
    renderer->SupportsBlendMode = GL_SupportsBlendMode;
    renderer->CreateTexture = GL_CreateTexture;
    renderer->UpdateTexture = GL_UpdateTexture;
#if SDL_HAVE_YUV
    renderer->UpdateTextureYUV = GL_UpdateTextureYUV;
    renderer->UpdateTextureNV = GL_UpdateTextureNV;
#endif
    renderer->LockTexture = GL_LockTexture;
    renderer->UnlockTexture = GL_UnlockTexture;
    renderer->SetTextureScaleMode = GL_SetTextureScaleMode;
    renderer->SetRenderTarget = GL_SetRenderTarget;
    renderer->QueueSetViewport = GL_QueueSetViewport;
    renderer->QueueSetDrawColor = GL_QueueSetViewport; /* SetViewport and SetDrawColor are (currently) no-ops. */
    renderer->QueueDrawPoints = GL_QueueDrawPoints;
    renderer->QueueDrawLines = GL_QueueDrawLines;
    renderer->QueueGeometry = GL_QueueGeometry;
    renderer->RunCommandQueue = GL_RunCommandQueue;
    renderer->RenderReadPixels = GL_RenderReadPixels;
    renderer->RenderPresent = GL_RenderPresent;
    renderer->DestroyTexture = GL_DestroyTexture;
    renderer->DestroyRenderer = GL_DestroyRenderer;
    renderer->SetVSync = GL_SetVSync;
    renderer->GL_BindTexture = GL_BindTexture;
    renderer->GL_UnbindTexture = GL_UnbindTexture;
    renderer->info = GL_RenderDriver.info;
    renderer->info.flags = 0; /* will set some flags below. */
    renderer->driverdata = data;
    renderer->window = window;

#ifdef SDL_VIDEO_DRIVER_DREAMCAST
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);  // OpenGL 1.x for compatibility
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
data->context = SDL_GL_CreateContext(window);
if (!data->context) {
        SDL_free(data);
        goto error;
    }
    if (SDL_GL_MakeCurrent(window, data->context) < 0) {
#ifdef SDL_VIDEO_DRIVER_DREAMCAST
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to make OpenGL context current on Dreamcast.");
#endif
        SDL_GL_DeleteContext(data->context);
        SDL_free(data);
        goto error;
    }

    if (GL_LoadFunctions(data) < 0) {
        SDL_GL_DeleteContext(data->context);
        SDL_free(data);
        goto error;
    }

    if (GL_IsProbablyAccelerated(data)) {
        renderer->info.flags |= SDL_RENDERER_ACCELERATED;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "OpenGL acceleration enabled.");
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "OpenGL acceleration not enabled.");
    }

#ifdef __MACOSX__
    /* Enable multi-threaded rendering */
    /* Disabled until Ryan finishes his VBO/PBO code...
       CGLEnable(CGLGetCurrentContext(), kCGLCEMPEngine);
     */
#endif

    if (flags & SDL_RENDERER_PRESENTVSYNC) {
        SDL_GL_SetSwapInterval(1);
    } else {
        SDL_GL_SetSwapInterval(0);
    }
    if (SDL_GL_GetSwapInterval() != 0) {
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "VSync enabled.");
    }

    /* Check for debug output support */
    if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &value) == 0 &&
        (value & SDL_GL_CONTEXT_DEBUG_FLAG)) {
        data->debug_enabled = SDL_TRUE;
#ifdef SDL_VIDEO_DRIVER_DREAMCAST
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "OpenGL debug output enabled.");
#endif
    }
    if (data->debug_enabled && SDL_GL_ExtensionSupported("GL_ARB_debug_output")) {
        PFNGLDEBUGMESSAGECALLBACKARBPROC glDebugMessageCallbackARBFunc = (PFNGLDEBUGMESSAGECALLBACKARBPROC)SDL_GL_GetProcAddress("glDebugMessageCallbackARB");

        data->GL_ARB_debug_output_supported = SDL_TRUE;
        data->glGetPointerv(GL_DEBUG_CALLBACK_FUNCTION_ARB, (GLvoid **)(char *)&data->next_error_callback);
        data->glGetPointerv(GL_DEBUG_CALLBACK_USER_PARAM_ARB, &data->next_error_userparam);
        glDebugMessageCallbackARBFunc(GL_HandleDebugMessage, renderer);

        /* Make sure our callback is called when errors actually happen */
        data->glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
    }

    hint = SDL_getenv("GL_ARB_texture_non_power_of_two");
    if (!hint || *hint != '0') {
        SDL_bool isGL2 = SDL_FALSE;
        const char *verstr = (const char *)data->glGetString(GL_VERSION);
        if (verstr) {
            char verbuf[16];
            char *ptr;
            SDL_strlcpy(verbuf, verstr, sizeof(verbuf));
            ptr = SDL_strchr(verbuf, '.');
            if (ptr) {
                *ptr = '\0';
                if (SDL_atoi(verbuf) >= 2) {
                    isGL2 = SDL_TRUE;
                }
            }
        }
        if (isGL2 || SDL_GL_ExtensionSupported("GL_ARB_texture_non_power_of_two")) {
            non_power_of_two_supported = SDL_TRUE;
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Non-power-of-two textures supported.");
        }
    }

    data->textype = GL_TEXTURE_2D;
    if (non_power_of_two_supported) {
        data->GL_ARB_texture_non_power_of_two_supported = SDL_TRUE;
        data->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        renderer->info.max_texture_width = value;
        renderer->info.max_texture_height = value;
    } else if (SDL_GL_ExtensionSupported("GL_ARB_texture_rectangle") ||
               SDL_GL_ExtensionSupported("GL_EXT_texture_rectangle")) {
        data->GL_ARB_texture_rectangle_supported = SDL_TRUE;
        data->textype = GL_TEXTURE_RECTANGLE_ARB;
        data->glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB, &value);
        renderer->info.max_texture_width = value;
        renderer->info.max_texture_height = value;
    } else {
        data->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        renderer->info.max_texture_width = value;
        renderer->info.max_texture_height = value;
    }

    /* Check for multitexture support */
    if (SDL_GL_ExtensionSupported("GL_ARB_multitexture")) {
        data->glActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC)SDL_GL_GetProcAddress("glActiveTextureARB");
        if (data->glActiveTextureARB) {
            data->GL_ARB_multitexture_supported = SDL_TRUE;
            data->glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &data->num_texture_units);
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Multitexture supported.");
        }
    }

    /* Check for shader support */
    if (SDL_GetHintBoolean(SDL_HINT_RENDER_OPENGL_SHADERS, SDL_TRUE)) {
        data->shaders = GL_CreateShaderContext();
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "OpenGL shaders: %s",
                data->shaders ? "ENABLED" : "DISABLED");
#if SDL_HAVE_YUV
    /* We support YV12 textures using 3 textures and a shader */
    if (data->shaders && data->num_texture_units >= 3) {
        renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_YV12;
        renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_IYUV;
    }

    /* We support NV12 textures using 2 textures and a shader */
    if (data->shaders && data->num_texture_units >= 2) {
        renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_NV12;
        renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_NV21;
    }
#endif
#ifdef __MACOSX__
    renderer->info.texture_formats[renderer->info.num_texture_formats++] = SDL_PIXELFORMAT_UYVY;
#endif

    renderer->rect_index_order[0] = 0;
    renderer->rect_index_order[1] = 1;
    renderer->rect_index_order[2] = 3;
    renderer->rect_index_order[3] = 1;
    renderer->rect_index_order[4] = 3;
    renderer->rect_index_order[5] = 2;

    if (SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object")) {
        data->GL_EXT_framebuffer_object_supported = SDL_TRUE;
        data->glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)
            SDL_GL_GetProcAddress("glGenFramebuffersEXT");
        data->glDeleteFramebuffersEXT = (PFNGLDELETEFRAMEBUFFERSEXTPROC)
            SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
        data->glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
            SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
        data->glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)
            SDL_GL_GetProcAddress("glBindFramebufferEXT");
        data->glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
            SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
        renderer->info.flags |= SDL_RENDERER_TARGETTEXTURE;
    }
    data->framebuffers = NULL;

    /* Set up parameters for rendering */
    data->glMatrixMode(GL_MODELVIEW);
    data->glLoadIdentity();
    data->glDisable(GL_DEPTH_TEST);
    data->glDisable(GL_CULL_FACE);
    data->glDisable(GL_SCISSOR_TEST);
    data->glDisable(data->textype);
    data->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    data->glColor4ub(255, 255, 255, 255);
    /* This ended up causing video discrepancies between OpenGL and Direct3D */
    /* data->glEnable(GL_LINE_SMOOTH); */

    data->drawstate.blend = SDL_BLENDMODE_INVALID;
    data->drawstate.shader = SHADER_INVALID;
    data->drawstate.color = 0xFFFFFFFF;
    data->drawstate.clear_color = 0xFFFFFFFF;

    return 0;

error:
    if (changed_window) {
        /* Uh oh, better try to put it back... */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile_mask);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        SDL_RecreateWindow(window, window_flags);
    }
    return -1;
}

SDL_RenderDriver GL_RenderDriver = {
    GL_CreateRenderer,
    { "opengl",
      (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE),
      4,
#ifdef __DREAMCAST__
      {
        SDL_PIXELFORMAT_ARGB1555,    // optimal - native PVR with alpha
        // SDL_PIXELFORMAT_RGB565,   // native PVR, but should not be selected as the streaming texture fallback
        // SDL_PIXELFORMAT_ARGB4444, // native PVR with alpha, but not used by SDL's Dreamcast texture fallback
        SDL_PIXELFORMAT_RGB24,       // fallback - works but not optimal
        SDL_PIXELFORMAT_RGB888,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_PIXELFORMAT_XRGB8888,
      },
#else
      { SDL_PIXELFORMAT_ARGB8888,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_PIXELFORMAT_RGB888,
        SDL_PIXELFORMAT_BGR888 },
#endif      

      0,
      0 }
};

#endif /* SDL_VIDEO_RENDER_OGL */

/* vi: set ts=4 sw=4 expandtab: */
