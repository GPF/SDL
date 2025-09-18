#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

// 3D vertex structure
typedef struct {
    float x, y, z;
} Vec3;

// Simple 3D cube vertices
Vec3 cube_vertices[8] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},  // Back face
    {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}   // Front face
};

// Cube edges (pairs of vertex indices)
int cube_edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},  // Back face
    {4, 5}, {5, 6}, {6, 7}, {7, 4},  // Front face
    {0, 4}, {1, 5}, {2, 6}, {3, 7}   // Connecting edges
};

// Cube faces for filled rendering (4 vertices per face)
int cube_faces[6][4] = {
    {0, 1, 2, 3},  // Back
    {7, 6, 5, 4},  // Front
    {4, 5, 1, 0},  // Bottom
    {3, 2, 6, 7},  // Top
    {0, 3, 7, 4},  // Left
    {1, 5, 6, 2}   // Right
};

// Face colors (using SDL_FColor for SDL3)
SDL_FColor face_colors[6] = {
    {1.0f, 0.0f, 0.0f, 1.0f},  // Red
    {0.0f, 1.0f, 0.0f, 1.0f},  // Green  
    {0.0f, 0.0f, 1.0f, 1.0f},  // Blue
    {0.0f, 1.0f, 1.0f, 1.0f},  // Cyan
    {1.0f, 0.0f, 1.0f, 1.0f},  // Magenta
    {1.0f, 1.0f, 1.0f, 1.0f}   // White
};

// Project 3D point to 2D screen coordinates using SDL_FPoint
SDL_FPoint project_3d_to_2d(Vec3 point, float distance) {
    SDL_FPoint result;
    float projected_x = (point.x * distance) / (point.z + distance);
    float projected_y = (point.y * distance) / (point.z + distance);
    
    result.x = projected_x * 200.0f + SCREEN_WIDTH / 2.0f;
    result.y = projected_y * 200.0f + SCREEN_HEIGHT / 2.0f;
    
    return result;
}

// Rotate a 3D point around Y axis
Vec3 rotate_y(Vec3 point, float angle) {
    Vec3 result;
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    
    result.x = point.x * cos_a + point.z * sin_a;
    result.y = point.y;
    result.z = -point.x * sin_a + point.z * cos_a;
    
    return result;
}

// Rotate a 3D point around X axis
Vec3 rotate_x(Vec3 point, float angle) {
    Vec3 result;
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    
    result.x = point.x;
    result.y = point.y * cos_a - point.z * sin_a;
    result.z = point.y * sin_a + point.z * cos_a;
    
    return result;
}

// Calculate face normal for backface culling
Vec3 calculate_face_normal(Vec3 v0, Vec3 v1, Vec3 v2) {
    Vec3 edge1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
    Vec3 edge2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
    
    Vec3 normal;
    normal.x = edge1.y * edge2.z - edge1.z * edge2.y;
    normal.y = edge1.z * edge2.x - edge1.x * edge2.z;
    normal.z = edge1.x * edge2.y - edge1.y * edge2.x;
    
    return normal;
}

// Create SDL_Vertex from screen position and color
SDL_Vertex create_vertex(SDL_FPoint pos, SDL_FColor color) {
    SDL_Vertex vertex;
    vertex.position = pos;
    vertex.color = color;
    vertex.tex_coord.x = 0.0f;  // No texture
    vertex.tex_coord.y = 0.0f;
    return vertex;
}

int main(int argc, char* argv[]) {
    // Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create window
    SDL_Window* window = SDL_CreateWindow(
        "SDL3 3D Demo - Using Built-in Geometry",
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Main loop variables
    int running = 1;
    SDL_Event event;
    Uint64 last_time = SDL_GetTicks();
    float rotation_x = 0.0f;
    float rotation_y = 0.0f;
    int render_mode = 0; // 0 = wireframe, 1 = filled

    printf("Controls:\n");
    printf("SPACE - Toggle between wireframe and filled rendering\n");
    printf("ESC - Exit\n");

    while (running) {
        // Handle events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        running = 0;
                    } else if (event.key.key == SDLK_SPACE) {
                        render_mode = 1 - render_mode;
                        printf("Render mode: %s\n", render_mode ? "Filled" : "Wireframe");
                    }
                    break;
            }
        }

        // Update rotation
        Uint64 current_time = SDL_GetTicks();
        float delta_time = (current_time - last_time) / 1000.0f;
        last_time = current_time;
        
        rotation_x += 0.5f * delta_time;
        rotation_y += 0.7f * delta_time;

        // Clear screen
        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
        SDL_RenderClear(renderer);

        // Transform and project vertices
        SDL_FPoint projected_vertices[8];
        Vec3 transformed_vertices[8];
        
        for (int i = 0; i < 8; i++) {
            Vec3 vertex = cube_vertices[i];
            
            // Apply rotations
            vertex = rotate_x(vertex, rotation_x);
            vertex = rotate_y(vertex, rotation_y);
            
            // Move cube away from camera
            vertex.z += 5.0f;
            
            transformed_vertices[i] = vertex;
            projected_vertices[i] = project_3d_to_2d(vertex, 3.0f);
        }

        if (render_mode == 0) {
            // Wireframe rendering using individual SDL_RenderLine calls
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            
            for (int i = 0; i < 12; i++) {
                int v1 = cube_edges[i][0];
                int v2 = cube_edges[i][1];
                
                SDL_RenderLine(renderer, 
                    projected_vertices[v1].x, projected_vertices[v1].y,
                    projected_vertices[v2].x, projected_vertices[v2].y);
            }
        } else {
            // Filled rendering using SDL_RenderGeometry with backface culling
            for (int face = 0; face < 6; face++) {
                int v0 = cube_faces[face][0];
                int v1 = cube_faces[face][1];
                int v2 = cube_faces[face][2];
                int v3 = cube_faces[face][3];
                
                // Calculate face normal for backface culling
                Vec3 normal = calculate_face_normal(
                    transformed_vertices[v0],
                    transformed_vertices[v1], 
                    transformed_vertices[v2]
                );
                
                // Simple backface culling (if normal.z > 0, face is facing camera)
                if (normal.z > 0) {
                    // Create vertices for two triangles (quad = 2 triangles)
                    SDL_Vertex triangle_vertices[6];
                    
                    // First triangle: v0, v1, v2
                    triangle_vertices[0] = create_vertex(projected_vertices[v0], face_colors[face]);
                    triangle_vertices[1] = create_vertex(projected_vertices[v1], face_colors[face]);
                    triangle_vertices[2] = create_vertex(projected_vertices[v2], face_colors[face]);
                    
                    // Second triangle: v0, v2, v3
                    triangle_vertices[3] = create_vertex(projected_vertices[v0], face_colors[face]);
                    triangle_vertices[4] = create_vertex(projected_vertices[v2], face_colors[face]);
                    triangle_vertices[5] = create_vertex(projected_vertices[v3], face_colors[face]);
                    
                    // Render the triangles using SDL3's geometry renderer
                    SDL_RenderGeometry(renderer, NULL, triangle_vertices, 6, NULL, 0);
                }
            }
        }

        // Present the rendered frame
        SDL_RenderPresent(renderer);
        
        // Cap frame rate to ~60 FPS
        SDL_Delay(16);
    }

    // Cleanup
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}