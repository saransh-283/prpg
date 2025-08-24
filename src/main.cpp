#include <iostream>
#include <string>
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include "objects/text/text_renderer.h"

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

    // Initialize the project's text overlay system
    InitTextOverlay();

    bool running = true;
    Uint32 lastTicks = SDL_GetTicks();

    while (running) {
        Uint32 now = SDL_GetTicks();
        float delta = (now - lastTicks) / 1000.0f;
        lastTicks = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Example overlay text lines
        RenderTextOverlay("PROCGEN - Minimal Text Demo", 10.0f, 10.0f, windowW, windowH);

        char buf[128];
        snprintf(buf, sizeof(buf), "Delta: %.3f s | FPS: %.1f", delta, (delta > 0.0f ? 1.0f / delta : 0.0f));
        RenderTextOverlay(buf, 10.0f, 40.0f, windowW, windowH);

        RenderTextOverlay("Press ESC or close window to exit", 10.0f, 70.0f, windowW, windowH);

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
