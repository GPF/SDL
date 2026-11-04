#include <SDL2/SDL.h>
#include <stdbool.h>
#include <math.h>
#include <kos.h>

#define WIN_W 640
#define WIN_H 480
#define TEX_W 128
#define TEX_H 128

#define PORTAL_X 220
#define PORTAL_Y 140
#define PORTAL_W 200
#define PORTAL_H 200

typedef struct {
    float x, y;
    float vx, vy;
    float size;
    SDL_Color color;
} Object2D;

static Object2D g_objects[8];
static float g_time = 0.0f;
static SDL_Texture *g_portal_texture = NULL;

static void setup_objects(void)
{
    static const SDL_Color colors[8] = {
        {255,  51,  51, 255},
        { 51, 255,  51, 255},
        { 51,  51, 255, 255},
        {255, 255,  51, 255},
        {255,  51, 255, 255},
        { 51, 255, 255, 255},
        {255, 128,  51, 255},
        {128,  51, 255, 255},
    };

    for (int i = 0; i < 8; ++i) {
        g_objects[i].x = (i % 3 - 1) * 1.2f;
        g_objects[i].y = (i / 3 - 1) * 1.0f;
        g_objects[i].vx = (i * 0.2f + 0.1f) * ((i & 1) ? 1.0f : -1.0f);
        g_objects[i].vy = (i * 0.15f + 0.1f) * ((i % 3) ? 1.0f : -1.0f);
        g_objects[i].size = 0.3f + i * 0.05f;
        g_objects[i].color = colors[i];
    }
}

static void update_objects(float dt)
{
    for (int i = 0; i < 8; ++i) {
        Object2D *obj = &g_objects[i];
        obj->x += obj->vx * dt;
        obj->y += obj->vy * dt;

        if (obj->x > 2.5f || obj->x < -2.5f) {
            obj->vx = -obj->vx;
        }
        if (obj->y > 2.5f || obj->y < -2.5f) {
            obj->vy = -obj->vy;
        }
    }
}

static void draw_filled_rect(SDL_Renderer *ren, int x, int y, int w, int h,
                             Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderFillRect(ren, &rect);
}

static void draw_portal_texture(SDL_Renderer *ren)
{
    SDL_Rect rect;

    SDL_SetRenderTarget(ren, g_portal_texture);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    draw_filled_rect(ren, 0, 0, TEX_W, TEX_H, 8, 12, 24, 255);

    SDL_SetRenderDrawColor(ren, 30, 30, 50, 255);
    for (int i = 0; i < TEX_W; i += 8) {
        rect.x = i;
        rect.y = 0;
        rect.w = 1;
        rect.h = TEX_H;
        SDL_RenderFillRect(ren, &rect);

        rect.x = 0;
        rect.y = i;
        rect.w = TEX_W;
        rect.h = 1;
        SDL_RenderFillRect(ren, &rect);
    }

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    for (int i = 0; i < 8; ++i) {
        const Object2D *obj = &g_objects[i];
        float x = obj->x * 12.0f + TEX_W / 2.0f;
        float y = obj->y * 12.0f + TEX_H / 2.0f;
        float size = obj->size * 20.0f + 8.0f;

        draw_filled_rect(ren,
                         (int)(x - size / 2.0f),
                         (int)(y - size / 2.0f),
                         (int)size,
                         (int)size,
                         obj->color.r, obj->color.g, obj->color.b, 255);

        draw_filled_rect(ren,
                         (int)(x - size / 4.0f),
                         (int)(y - size / 4.0f),
                         (int)(size / 4.0f),
                         (int)(size / 4.0f),
                         255, 255, 255, 255);
    }

    {
        float cx = TEX_W / 2.0f;
        float cy = TEX_H / 2.0f;
        for (int i = 0; i < 20; ++i) {
            float angle = g_time + i * 0.3f;
            float radius = 20.0f + sinf(g_time * 0.5f + i) * 5.0f;
            float x = cx + cosf(angle) * radius;
            float y = cy + sinf(angle * 0.5f + g_time) * radius;
            draw_filled_rect(ren, (int)x - 1, (int)y - 1, 4, 4, 150, 50, 255, 255);
        }
    }

    {
        float player_x = (sinf(g_time * 0.5f) * 0.5f + 0.5f) * (TEX_W - 20.0f);
        draw_filled_rect(ren, (int)player_x, 20, 20, 20, 255, 0, 0, 255);
        draw_filled_rect(ren, (int)player_x + 5, 28, 10, 8, 0, 255, 0, 255);
    }

    SDL_SetRenderTarget(ren, NULL);
}

static void draw_main_scene(SDL_Renderer *ren)
{
    SDL_Rect rect;

    SDL_SetRenderDrawColor(ren, 10, 10, 20, 255);
    SDL_RenderClear(ren);

    draw_filled_rect(ren, 0, 0, WIN_W, 20, 40, 30, 60, 255);
    draw_filled_rect(ren, 0, WIN_H - 20, WIN_W, 20, 40, 30, 60, 255);
    draw_filled_rect(ren, 0, 0, 20, WIN_H, 40, 30, 60, 255);
    draw_filled_rect(ren, WIN_W - 20, 0, 20, WIN_H, 40, 30, 60, 255);
    draw_filled_rect(ren, 230, 10, 180, 20, 100, 80, 200, 255);

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
    draw_filled_rect(ren, PORTAL_X - 16, PORTAL_Y - 16,
                     PORTAL_W + 32, PORTAL_H + 32, 30, 10, 60, 100);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    draw_filled_rect(ren, PORTAL_X - 8, PORTAL_Y - 8, PORTAL_W + 16, 8, 80, 50, 150, 255);
    draw_filled_rect(ren, PORTAL_X - 8, PORTAL_Y + PORTAL_H, PORTAL_W + 16, 8, 80, 50, 150, 255);
    draw_filled_rect(ren, PORTAL_X - 8, PORTAL_Y, 8, PORTAL_H, 80, 50, 150, 255);
    draw_filled_rect(ren, PORTAL_X + PORTAL_W, PORTAL_Y, 8, PORTAL_H, 80, 50, 150, 255);

    rect.x = PORTAL_X;
    rect.y = PORTAL_Y;
    rect.w = PORTAL_W;
    rect.h = PORTAL_H;
    SDL_RenderCopyEx(ren, g_portal_texture, NULL, &rect, 0.0, NULL, SDL_FLIP_VERTICAL);

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    draw_filled_rect(ren, PORTAL_X, PORTAL_Y, PORTAL_W, PORTAL_H, 0, 0, 0, 80);

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 20; ++i) {
        float angle = g_time + i * 1.2f;
        float dist = 1.0f + sinf(g_time * 0.5f + i) * 0.3f;
        float px = PORTAL_X + PORTAL_W / 2.0f + cosf(angle) * dist * PORTAL_W / 2.0f * 0.6f;
        float py = PORTAL_Y + PORTAL_H / 2.0f + sinf(angle * 0.7f + g_time) * dist * PORTAL_H / 2.0f * 0.6f;
        int size = 3 + (int)(sinf(g_time + i) * 2.0f);
        if ((i % 3) == 0) {
            draw_filled_rect(ren, (int)px, (int)py, size, size, 150, 50, 255, 255);
        } else if ((i % 3) == 1) {
            draw_filled_rect(ren, (int)px, (int)py, size, size, 50, 150, 255, 255);
        } else {
            draw_filled_rect(ren, (int)px, (int)py, size, size, 255, 50, 150, 255);
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    cont_btn_callback(0, CONT_START | CONT_A | CONT_B | CONT_X | CONT_Y,
                      (cont_btn_callback_t)arch_exit);

    SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "SDL2 FBO Simple",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE
    );
    if (!ren) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    g_portal_texture = SDL_CreateTexture(
        ren,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_TARGET,
        TEX_W, TEX_H
    );
    if (!g_portal_texture) {
        SDL_Log("SDL_CreateTexture(target) failed: %s", SDL_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(g_portal_texture, SDL_ScaleModeNearest);
    SDL_Log("Created portal target texture %dx%d", TEX_W, TEX_H);

    setup_objects();

    bool running = true;
    int frame = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
        }

        g_time += 0.016f;
        update_objects(0.016f);

        SDL_SetRenderTarget(ren, g_portal_texture);
        draw_portal_texture(ren);

        SDL_SetRenderTarget(ren, NULL);
        draw_main_scene(ren);
        SDL_RenderPresent(ren);

        if ((frame % 60) == 0) {
            SDL_Log("frame %d", frame);
        }
        ++frame;

        SDL_Delay(16);
    }

    SDL_DestroyTexture(g_portal_texture);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
