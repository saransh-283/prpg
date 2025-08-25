#include <iostream>
#include <string>
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "objects/text/text_renderer.h"
#include "objects/models/3d/cube/mesh.h"
#include "objects/models/3d/custom/mesh.h"
#include "objects/models/3d/sphere/mesh.h"
#include "utils/shaders/shader_utils.h"
// Wireframe toggle for triangulated meshes
#include "utils/triangulate/mesh.h"
// Infinite terrain
#include "world/terrain/terrain.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    std::cout << "Procgen (minimal) - text overlay demo" << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Request a depth buffer
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    const int windowW = 1200;
    const int windowH = 800;
    SDL_Window *window = SDL_CreateWindow("Procgen - Text Only",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          windowW, windowH,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

    if (!window)
    {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext)
    {
        std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "OpenGL: " << glGetString(GL_VERSION) << std::endl;

    glViewport(0, 0, windowW, windowH);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Enable depth testing for 3D rendering
    glEnable(GL_DEPTH_TEST);

    // Initialize the project's text overlay system
    InitTextOverlay();

    // Initialize terrain system
    if (!InitTerrain()) {
        std::cerr << "Failed to initialize terrain" << std::endl;
    }

    // --- Load simple 3D shader from files ---
    GLuint program3D = 0;
    if (!LoadShaderProgram("src/utils/shaders/simple3d.vert", "src/utils/shaders/simple3d.frag", program3D))
    {
        std::cerr << "Failed to load 3D shader program" << std::endl;
        program3D = 0;
    }

    bool running = true;
    Uint32 lastTicks = SDL_GetTicks();
    bool wireframeMode = false;

    // No external objects: terrain-only scene

    // Camera/projection setup
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)windowW / (float)windowH, 0.1f, 100.0f);
    // Camera state (position, orientation)
    glm::vec3 cameraPos(0.0f, 0.0f, 3.0f);
    glm::vec3 cameraFront(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);
    float yaw = -90.0f; // degrees, -Z
    float pitch = 0.0f; // degrees
    const float mouseSensitivity = 0.12f;
    const float moveSpeed = 2.5f; // units per second

    // Enable relative mouse mode for FPS-like look
    bool mouseCaptured = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    // Flying mode toggle
    bool flying = false;

    float angle = 0.0f;

    // Player physics
    glm::vec3 velocity(0.0f);
    const float gravity = -9.81f;
    bool onGround = false;

    while (running)
    {
        Uint32 now = SDL_GetTicks();
        float delta = (now - lastTicks) / 1000.0f;
        lastTicks = now;

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = false;
            if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
                // Toggle global wireframe mode with 'F'
                if (e.key.keysym.sym == SDLK_f)
                {
                    wireframeMode = !wireframeMode;
                    SetGlobalWireframeMode(wireframeMode);
                }
                // Toggle mouse capture with 'M'
                if (e.key.keysym.sym == SDLK_m)
                {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
                }
                // Toggle flying mode with Space
                if (e.key.keysym.sym == SDLK_SPACE)
                {
                    flying = !flying;
                    if (flying)
                    {
                        // stop any falling motion when entering fly
                        velocity.y = 0.0f;
                        onGround = false;
                    }
                    else
                    {
                        // when disabling fly, ensure we're not below ground
                        float groundY = SampleTerrainHeight(cameraPos.x, cameraPos.z) + 0.5f;
                        if (cameraPos.y < groundY)
                            cameraPos.y = groundY;
                    }
                }
            }

            // Mouse motion for looking around
            if (e.type == SDL_MOUSEMOTION)
            {
                float xrel = (float)e.motion.xrel;
                float yrel = (float)e.motion.yrel;
                yaw += xrel * mouseSensitivity;
                pitch -= yrel * mouseSensitivity; // invert Y
                if (pitch > 89.0f)
                    pitch = 89.0f;
                if (pitch < -89.0f)
                    pitch = -89.0f;
                glm::vec3 front;
                front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
                front.y = sin(glm::radians(pitch));
                front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
                cameraFront = glm::normalize(front);
            }
        }

    // Continuous keyboard state for movement (WASD) + jump
        const Uint8 *kb = SDL_GetKeyboardState(NULL);
        if (kb[SDL_SCANCODE_W])
        {
            cameraPos += cameraFront * moveSpeed * delta;
        }
        if (kb[SDL_SCANCODE_S])
        {
            cameraPos -= cameraFront * moveSpeed * delta;
        }
        // Right vector
        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
        if (kb[SDL_SCANCODE_A])
        {
            cameraPos -= right * moveSpeed * delta;
        }
        if (kb[SDL_SCANCODE_D])
        {
            cameraPos += right * moveSpeed * delta;
        }

        // Flying vertical control: Up/Down when flying
        if (flying)
        {
            const float flySpeed = 3.0f; // vertical units per second while flying
            if (kb[SDL_SCANCODE_UP])
            {
                cameraPos.y += flySpeed * delta;
            }
            if (kb[SDL_SCANCODE_DOWN])
            {
                cameraPos.y -= flySpeed * delta;
            }
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Apply gravity to player (unless flying)
        if (!flying)
        {
            velocity.y += gravity * delta;
            cameraPos += velocity * delta;

            // Ensure player stays above terrain
            float groundY = SampleTerrainHeight(cameraPos.x, cameraPos.z) + 0.5f; // eye offset
            if (cameraPos.y <= groundY) {
                cameraPos.y = groundY;
                velocity.y = 0.0f;
                onGround = true;
            } else {
                onGround = false;
            }
        }

        // Render 3D shapes
        if (program3D)
        {
            glUseProgram(program3D);

            angle += delta * 1.0f; // radians per second

            // Update view matrix from camera
            glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

            // Update terrain generation around camera
            UpdateTerrain(cameraPos);

            // Uniform locations reused for all objects
            GLint loc = glGetUniformLocation(program3D, "uMVP");
            GLint colorLoc = glGetUniformLocation(program3D, "uColor");

            // Render terrain (uses same simple3d shader)
            RenderTerrain(program3D, proj, view);
        }

        // Example overlay text lines
        RenderTextOverlay("PROCGEN - Minimal Text Demo", 10.0f, 10.0f, windowW, windowH);
        RenderTextOverlay("Press ESC or close window to exit", 10.0f, 70.0f, windowW, windowH);
        char wfbuf[64];
        snprintf(wfbuf, sizeof(wfbuf), "Wireframe: %s (press 'F' to toggle)", (wireframeMode ? "ON" : "OFF"));
        RenderTextOverlay(wfbuf, 10.0f, 100.0f, windowW, windowH);

        SDL_GL_SwapWindow(window);

        // Cap to ~60 FPS
        if (delta < (1.0f / 60.0f))
            SDL_Delay((Uint32)(((1.0f / 60.0f) - delta) * 1000.0f));
    }

    CleanupTextOverlay();

    CleanupTerrain();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Exiting." << std::endl;
    return 0;
}
