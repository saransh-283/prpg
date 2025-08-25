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

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    std::cout << "Procgen (minimal) - text overlay demo" << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
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
    SDL_Window* window = SDL_CreateWindow("Procgen - Text Only",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          windowW, windowH,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
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

    // --- Load simple 3D shader from files ---
    GLuint program3D = 0;
    if (!LoadShaderProgram("src/utils/shaders/simple3d.vert", "src/utils/shaders/simple3d.frag", program3D)) {
        std::cerr << "Failed to load 3D shader program" << std::endl;
        program3D = 0;
    }

    bool running = true;
    Uint32 lastTicks = SDL_GetTicks();
    bool wireframeMode = false;

    // Create meshes: load custom model (replace cube)
    CustomMesh customMesh = CreateCustomMesh("src/assets/objects/models/3d/custom/Custom.glb");

    SphereMesh sphereMesh = CreateSphereMesh(0.0f, 0.0f, 0.0f, 0.4f, 16, 24);

    // Camera/projection setup
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), (float)windowW / (float)windowH, 0.1f, 100.0f);
    // Camera state (position, orientation)
    glm::vec3 cameraPos(0.0f, 0.0f, 3.0f);
    glm::vec3 cameraFront(0.0f, 0.0f, -1.0f);
    glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);
    float yaw = -90.0f;   // degrees, -Z
    float pitch = 0.0f;   // degrees
    const float mouseSensitivity = 0.12f;
    const float moveSpeed = 2.5f; // units per second

    // Enable relative mouse mode for FPS-like look
    bool mouseCaptured = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);

    float angle = 0.0f;

    while (running) {
        Uint32 now = SDL_GetTicks();
        float delta = (now - lastTicks) / 1000.0f;
        lastTicks = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                // Toggle global wireframe mode with 'F'
                if (e.key.keysym.sym == SDLK_f) {
                    wireframeMode = !wireframeMode;
                    SetGlobalWireframeMode(wireframeMode);
                }
                // Toggle mouse capture with 'M'
                if (e.key.keysym.sym == SDLK_m) {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
                }
            }

            // Mouse motion for looking around
            if (e.type == SDL_MOUSEMOTION) {
                float xrel = (float)e.motion.xrel;
                float yrel = (float)e.motion.yrel;
                yaw += xrel * mouseSensitivity;
                pitch -= yrel * mouseSensitivity; // invert Y
                if (pitch > 89.0f) pitch = 89.0f;
                if (pitch < -89.0f) pitch = -89.0f;
                glm::vec3 front;
                front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
                front.y = sin(glm::radians(pitch));
                front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
                cameraFront = glm::normalize(front);
            }
        }

        // Continuous keyboard state for movement (WASD)
        const Uint8* kb = SDL_GetKeyboardState(NULL);
        if (kb[SDL_SCANCODE_W]) {
            cameraPos += cameraFront * moveSpeed * delta;
        }
        if (kb[SDL_SCANCODE_S]) {
            cameraPos -= cameraFront * moveSpeed * delta;
        }
        // Right vector
        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
        if (kb[SDL_SCANCODE_A]) {
            cameraPos -= right * moveSpeed * delta;
        }
        if (kb[SDL_SCANCODE_D]) {
            cameraPos += right * moveSpeed * delta;
        }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Render 3D shapes
        if (program3D) {
            glUseProgram(program3D);

            angle += delta * 1.0f; // radians per second

            // Update view matrix from camera
            glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

            // Uniform locations reused for all objects
            GLint loc = glGetUniformLocation(program3D, "uMVP");
            GLint colorLoc = glGetUniformLocation(program3D, "uColor");

            // Custom model on the left: draw all triangulate meshes (each triangle is a small mesh)
            if (!customMesh.triangles.empty()) {
                glm::mat4 modelCube = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
                modelCube = glm::rotate(modelCube, angle, glm::vec3(0.5f, 1.0f, 0.0f));
                glm::mat4 mvpCube = proj * view * modelCube;
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpCube));
                glUniform3f(colorLoc, 0.8f, 0.3f, 0.2f);

                for (const auto& cm : customMesh.triangles) {
                    glBindVertexArray(cm.VAO);
                    if (cm.EBO != 0) {
                        glDrawElements(GL_TRIANGLES, cm.indexCount, GL_UNSIGNED_INT, 0);
                    } else {
                        glDrawArrays(GL_TRIANGLES, 0, cm.vertexCount);
                    }
                }
            }

            // Sphere on the right
            glm::mat4 modelSphere = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            modelSphere = glm::rotate(modelSphere, -angle * 0.8f, glm::vec3(0.3f, 1.0f, 0.2f));
            glm::mat4 mvpSphere = proj * view * modelSphere;
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(mvpSphere));
            glUniform3f(colorLoc, 0.2f, 0.5f, 0.9f);

            // Draw sphere mesh (non-indexed)
            glBindVertexArray(sphereMesh.mesh.VAO);
            if (sphereMesh.mesh.EBO != 0) {
                glDrawElements(GL_TRIANGLES, sphereMesh.mesh.indexCount, GL_UNSIGNED_INT, 0);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, sphereMesh.vertexCount);
            }

            glBindVertexArray(0);
            glUseProgram(0);
        }

        // Example overlay text lines
        RenderTextOverlay("PROCGEN - Minimal Text Demo", 10.0f, 10.0f, windowW, windowH);

        char buf[128];
        snprintf(buf, sizeof(buf), "Delta: %.3f s | FPS: %.1f", delta, (delta > 0.0f ? 1.0f / delta : 0.0f));
        RenderTextOverlay(buf, 10.0f, 40.0f, windowW, windowH);

        RenderTextOverlay("Press ESC or close window to exit", 10.0f, 70.0f, windowW, windowH);
    char wfbuf[64];
    snprintf(wfbuf, sizeof(wfbuf), "Wireframe: %s (press 'F' to toggle)", (wireframeMode ? "ON" : "OFF"));
    RenderTextOverlay(wfbuf, 10.0f, 100.0f, windowW, windowH);

        SDL_GL_SwapWindow(window);

        // Cap to ~60 FPS
        if (delta < (1.0f / 60.0f)) SDL_Delay((Uint32)(((1.0f / 60.0f) - delta) * 1000.0f));
    }

    CleanupTextOverlay();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Exiting." << std::endl;
    return 0;
}
