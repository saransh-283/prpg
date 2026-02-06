#include <iostream>
#include <string>
#include <csignal>
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <core/config.h>
#include <core/resources.h>
#include <objects/text/text_renderer.h>
#include <ui/loading/loading.h>
#include <objects/models/3d/cube/mesh.h>
#include <objects/models/3d/custom/mesh.h>
#include <objects/models/3d/sphere/mesh.h>
#include <utils/shaders/shader_utils.h>
// LLM (loaded asynchronously after the loading screen)
#include <utils/llm/init/init.h>
#include <utils/llm/shutdown/shutdown.h>
#include <filesystem>
// Wireframe toggle for triangulated meshes
#include <utils/triangulate/mesh.h>
// Infinite terrain
#include <world/terrain/terrain.h>
// Map UI
#include <ui/map/map.h>
// Debug overlay (hidden by default, toggled with tilde/backquote)
#include <ui/debug/debug_overlay.h>
// Player entity
#include <entities/player/player.h>
// NPC entity
#include <entities/npc/npc.h>
// Deferred renderer and skybox
#include <rendering/deferred_renderer.h>
#include <rendering/skybox.h>

namespace {
volatile std::sig_atomic_t g_terminateRequested = 0;

void HandleTerminateSignal(int) {
    g_terminateRequested = 1;
}

bool TerminateRequested() {
    return g_terminateRequested != 0;
}
}

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
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, Config::Window::DEPTH_BUFFER_SIZE);

    const int windowW = Config::Window::WIDTH;
    const int windowH = Config::Window::HEIGHT;
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

    // Enable VSync (best-effort). Some drivers may not support it.
    if (SDL_GL_SetSwapInterval(1) != 0) {
        // Try adaptive VSync if regular VSync isn't supported.
        if (SDL_GL_SetSwapInterval(-1) != 0) {
            std::cerr << "Warning: Failed to enable VSync: " << SDL_GetError() << std::endl;
        }
    }

    std::cout << "OpenGL: " << glGetString(GL_VERSION) << std::endl;

    glViewport(0, 0, windowW, windowH);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Enable depth testing for 3D rendering
    glEnable(GL_DEPTH_TEST);

    // Handle Ctrl+C / SIGTERM as a clean exit (best-effort).
    std::signal(SIGINT, HandleTerminateSignal);
    std::signal(SIGTERM, HandleTerminateSignal);

    // Initialize the project's text overlay system
    InitTextOverlay();

    // Initialize the loading screen and enqueue initialization tasks so we can
    // show progress while performing GPU/CPU setup on the main thread.
    InitLoading();

    // Initialize terrain system (must be done before generating chunks so
    // SampleTerrainHeight and road generation use a seeded/initialized perlin).
    if (!InitTerrain()) {
        std::cerr << "Failed to initialize terrain" << std::endl;
    }

    // Enqueue tasks: they run one-per-update and can call initialization
    // functions that rely on an active GL context.
    // Instead of one big InitTerrain, enqueue per-chunk tasks for finer progress.
    // Use the same view radius as the terrain module (approx 3 chunks).
    const int initialViewRadius = Config::World::INITIAL_VIEW_RADIUS;
    // center at 0,0 for initial generation
    int centerCx = 0;
    int centerCz = 0;
    // initial camera/spawn holder used by the spawn determination task
    glm::vec3 initialCameraPos(0.0f, 0.0f, 3.0f);

    // Shader program variables; these can be populated by loading tasks.
    GLuint program3D = 0;
    GLuint markerSdfProgram = 0;
    GLuint terrainShader = 0;
    GLuint roadsShader = 0;
    GLuint highwaysShader = 0;
    GLuint streetsShader = 0;
    GLuint buildingsShader = 0;

    // Map shaders (chunk-data driven UI)
    GLuint mapBorderShader = 0;
    GLuint mapHighwaysShader = 0;
    GLuint mapRoadsShader = 0;
    GLuint mapStreetsShader = 0;
    GLuint mapBuildingsShader = 0;

    // NPC system can be initialized during loading once a spawn position is known.
    NpcSystem npcSystem;
    bool npcInitialized = false;

    // Initialize NPC resources early so per-chunk spawn tasks can run.
    AddLoadingTask([&](){
        npcInitialized = npcSystem.Initialize(initialCameraPos);
        return npcInitialized;
    }, "InitNPCs");

    for (int dz = -initialViewRadius; dz <= initialViewRadius; ++dz) {
        for (int dx = -initialViewRadius; dx <= initialViewRadius; ++dx) {
            int cx = centerCx + dx;
            int cz = centerCz + dz;
            // terrain mesh generation per chunk
            AddLoadingTask([cx, cz](){ return GenerateTerrainChunk(cx, cz); }, "GenChunk");

            // Deterministically spawn NPCs for this chunk (variable count per chunk).
            // Runs after terrain so height sampling is valid.
            AddLoadingTask([cx, cz, &npcSystem](){ npcSystem.SpawnForChunk(cx, cz); return true; }, "SpawnNPCs");
            // road generation for the same chunk as a separate task; after generating
            // roads check if we can determine a spawn from already-generated data
            AddLoadingTask([cx, cz, &initialCameraPos](){
                bool ok = GenerateRoadsForChunk(cx, cz);
                if (!ok) return false;
                // Check if we can determine a spawn from already-generated data
                glm::vec2 candidate = DetermineSpawnPoint(initialCameraPos.x, initialCameraPos.z, 2);
                // If we found a good spawn (not just the fallback position), use it and skip remaining chunks
                if (glm::length(candidate - glm::vec2(initialCameraPos.x, initialCameraPos.z)) > 0.1f) {
                    initialCameraPos.x = candidate.x;
                    initialCameraPos.z = candidate.y;
                    initialCameraPos.y = SampleTerrainHeight(candidate.x, candidate.y) + Config::Player::EYE_HEIGHT;
                    // Skip the remaining chunk-generation tasks only; keep essential tasks (e.g., NPC init).
                    ClearLoadingTasksWithPrefix("Gen");
                }
                return true;
            }, "GenRoads");
            // streets generation as a separate task after roads are generated
            AddLoadingTask([cx, cz](){
                GenerateStreetsForChunk(cx, cz);
                return true;
            }, "GenStreets");

            // buildings generation is part of the chunk pipeline, but keep it as a
            // distinct task so the loader displays explicit buildings progress.
            AddLoadingTask([cx, cz](){ return GenerateBuildingsForChunk(cx, cz); }, "GenBuildings");
        }
    }

    // After chunks are generated, determine spawn point near origin as a separate task
    // initialCameraPos will be modified by this task and used later to initialize cameraPos
    AddLoadingTask([&](){
        glm::vec2 spawn = DetermineSpawnPoint(initialCameraPos.x, initialCameraPos.z, 2);
        // store the spawn into the initialCameraPos variable capture; main will use it
        initialCameraPos.x = spawn.x;
        initialCameraPos.z = spawn.y;
        initialCameraPos.y = SampleTerrainHeight(spawn.x, spawn.y) + Config::Player::EYE_HEIGHT;
        return true;
    }, "DetermineSpawn");

    AddLoadingTask([&](){
        return LoadShaderProgram(Resources::Shaders::Simple3D::VERTEX, Resources::Shaders::Simple3D::FRAGMENT, program3D);
    }, "LoadSimple3DShader");

    // Load terrain shader
    AddLoadingTask([&](){
        return LoadShaderProgram(Resources::Shaders::Terrain::VERTEX, Resources::Shaders::Terrain::FRAGMENT, terrainShader);
    }, "LoadTerrainShader");

    // Load roads shader
    AddLoadingTask([&](){
        return LoadShaderProgram(Resources::Shaders::Roads::VERTEX, Resources::Shaders::Roads::FRAGMENT, roadsShader);
    }, "LoadRoadsShader");

    // Load highways shader
    AddLoadingTask([&](){
        return LoadShaderProgram(Resources::Shaders::Highways::VERTEX, Resources::Shaders::Highways::FRAGMENT, highwaysShader);
    }, "LoadHighwaysShader");

    // Load streets shader
    AddLoadingTask([&](){
        return LoadShaderProgram(Resources::Shaders::Streets::VERTEX, Resources::Shaders::Streets::FRAGMENT, streetsShader);
    }, "LoadStreetsShader");

    // Buildings: compile shader during loading so map rendering has it ready.
    AddLoadingTask([&](){
        return LoadShaderProgram(Resources::Shaders::Buildings::VERTEX, Resources::Shaders::Buildings::FRAGMENT, buildingsShader);
    }, "LoadBuildingsShader");

    // Initialize map synchronously so its resources are available even if
    // the loader early-exits after finding a spawn from generated roads.
    if (!InitMap()) {
        std::cerr << "Failed to initialize map" << std::endl;
    }
    AddLoadingTask([](){ InitDebugOverlay(); return true; }, "InitDebugOverlay");

    // A finalization task that does lightweight setup if needed
    AddLoadingTask([](){ return true; }, "FinalizeLoading");

    // Run a small loading loop until tasks complete. This keeps the window
    // responsive and draws the loading overlay using the same GL context.
    bool loading = true;
    Uint32 lastTicksLoad = SDL_GetTicks();
    while (loading)
    {
        Uint32 now = SDL_GetTicks();
        float delta = (now - lastTicksLoad) / 1000.0f;
        lastTicksLoad = now;

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT) {
                SDL_GL_DeleteContext(glContext);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }

            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                SDL_GL_DeleteContext(glContext);
                SDL_DestroyWindow(window);
                SDL_Quit();
                return 0;
            }
        }

        if (TerminateRequested()) {
            SDL_GL_DeleteContext(glContext);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 0;
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Run one loading step and render overlay
        UpdateLoading();
        RenderLoading(windowW, windowH);

        SDL_GL_SwapWindow(window);

        if (IsLoadingDone()) loading = false;

        // small sleep to avoid pegging CPU
        SDL_Delay(10);
    }

    // Kick off LLM model loading in the background after the loading screen.
    // This avoids blocking startup/gameplay on a heavy GGUF load.
    {
        namespace fs = std::filesystem;

        std::string modelPath = Resources::Models::LLM_DEFAULT_MODEL;
        const std::string fallbackPath = "src/models/Meta-Llama-3-8B-Instruct.Q5_K_M.gguf";

        if (!fs::exists(modelPath) && fs::exists(fallbackPath)) {
            modelPath = fallbackPath;
        }

        if (fs::exists(modelPath)) {
            std::cout << "[LLM] Starting background load: " << modelPath << std::endl;
            (void)start_llm_background_load(modelPath, Config::LLM::DEFAULT_GPU_LAYERS, Config::LLM::DEFAULT_CONTEXT_SIZE);
        } else {
            std::cout << "[LLM] Model not found; skipping background load (tried: "
                      << Resources::Models::LLM_DEFAULT_MODEL << ", " << fallbackPath << ")" << std::endl;
        }
    }

    // Try loading shader programs
    if (!program3D && !LoadShaderProgram(Resources::Shaders::Simple3D::VERTEX, Resources::Shaders::Simple3D::FRAGMENT, program3D))
    {
        std::cerr << "Failed to load 3D shader program" << std::endl;
        program3D = 0;
    }

    static GLuint wireframeProgram = 0;
    if (!wireframeProgram && !LoadShaderProgram(Resources::Shaders::Wireframe::VERTEX, Resources::Shaders::Wireframe::FRAGMENT, wireframeProgram))
    {
        std::cerr << "Failed to load wireframe shader program" << std::endl;
        wireframeProgram = 0;
    }

    if (!markerSdfProgram && !LoadShaderProgram(Resources::Shaders::UI::MarkerSDF::VERTEX, Resources::Shaders::UI::MarkerSDF::FRAGMENT, markerSdfProgram))
    {
        std::cerr << "Failed to load marker SDF shader program" << std::endl;
        markerSdfProgram = 0;
    }

    if (!terrainShader && !LoadShaderProgram(Resources::Shaders::Terrain::VERTEX, Resources::Shaders::Terrain::FRAGMENT, terrainShader))
    {
        std::cerr << "Failed to load terrain shader program" << std::endl;
        terrainShader = 0;
    }

    if (!roadsShader && !LoadShaderProgram(Resources::Shaders::Roads::VERTEX, Resources::Shaders::Roads::FRAGMENT, roadsShader))
    {
        std::cerr << "Failed to load roads shader program" << std::endl;
        roadsShader = 0;
    }

    if (!highwaysShader && !LoadShaderProgram(Resources::Shaders::Highways::VERTEX, Resources::Shaders::Highways::FRAGMENT, highwaysShader))
    {
        std::cerr << "Failed to load highways shader program" << std::endl;
        highwaysShader = 0;
    }

    if (!streetsShader && !LoadShaderProgram(Resources::Shaders::Streets::VERTEX, Resources::Shaders::Streets::FRAGMENT, streetsShader))
    {
        std::cerr << "Failed to load streets shader program" << std::endl;
        streetsShader = 0;
    }

    if (!buildingsShader && !LoadShaderProgram(Resources::Shaders::Buildings::VERTEX, Resources::Shaders::Buildings::FRAGMENT, buildingsShader))
    {
        std::cerr << "Failed to load buildings shader program" << std::endl;
        buildingsShader = 0;
    }

    if (!mapBorderShader && !LoadShaderProgram(Resources::Shaders::Map::Border::VERTEX, Resources::Shaders::Map::Border::FRAGMENT, mapBorderShader))
    {
        std::cerr << "Failed to load map border shader program" << std::endl;
        mapBorderShader = 0;
    }

    if (!mapHighwaysShader && !LoadShaderProgram(Resources::Shaders::Map::Highways::VERTEX, Resources::Shaders::Map::Highways::FRAGMENT, mapHighwaysShader))
    {
        std::cerr << "Failed to load map highways shader program" << std::endl;
        mapHighwaysShader = 0;
    }

    if (!mapRoadsShader && !LoadShaderProgram(Resources::Shaders::Map::Roads::VERTEX, Resources::Shaders::Map::Roads::FRAGMENT, mapRoadsShader))
    {
        std::cerr << "Failed to load map roads shader program" << std::endl;
        mapRoadsShader = 0;
    }

    if (!mapStreetsShader && !LoadShaderProgram(Resources::Shaders::Map::Streets::VERTEX, Resources::Shaders::Map::Streets::FRAGMENT, mapStreetsShader))
    {
        std::cerr << "Failed to load map streets shader program" << std::endl;
        mapStreetsShader = 0;
    }

    if (!mapBuildingsShader && !LoadShaderProgram(Resources::Shaders::Map::Buildings::VERTEX, Resources::Shaders::Map::Buildings::FRAGMENT, mapBuildingsShader))
    {
        std::cerr << "Failed to load map buildings shader program" << std::endl;
        mapBuildingsShader = 0;
    }

    bool running = true;
    Uint32 lastTicks = SDL_GetTicks();
    bool wireframeMode = false;

    // No external objects: terrain-only scene

    // Camera/projection setup
    glm::mat4 proj = glm::perspective(glm::radians(Config::Camera::FOV), (float)windowW / (float)windowH, Config::Camera::NEAR_PLANE, Config::Camera::FAR_PLANE);
    
    // Initialize player
    Player player;
    player.Initialize(initialCameraPos);

    // NPCs should already have been initialized during loading; fallback if not.
    if (!npcInitialized) {
        npcSystem.Initialize(initialCameraPos);
    }

    float angle = 0.0f;

    // Spawn point was computed during loading and cameraPos updated accordingly if available

    // Initialize debug overlay
    InitDebugOverlay();

    // Initialize deferred renderer
    if (!DeferredRenderer::Initialize(windowW, windowH)) {
        std::cerr << "Failed to initialize deferred renderer" << std::endl;
        running = false;
    }

    // Initialize skybox
    if (!Skybox::Initialize()) {
        std::cerr << "Failed to initialize skybox" << std::endl;
        running = false;
    }

    // Set sun properties
    DeferredRenderer::SetSunDirection(glm::vec3(
        Config::Rendering::Sun::DIRECTION_X,
        Config::Rendering::Sun::DIRECTION_Y,
        Config::Rendering::Sun::DIRECTION_Z
    ));
    DeferredRenderer::SetSunColor(glm::vec3(
        Config::Rendering::Sun::COLOR_R,
        Config::Rendering::Sun::COLOR_G,
        Config::Rendering::Sun::COLOR_B
    ));
    DeferredRenderer::SetSunIntensity(Config::Rendering::Sun::INTENSITY);

    // Match skybox to configured time-of-day.
    Skybox::SetTimeOfDay(Config::Rendering::Skybox::TIME_OF_DAY);

    while (running)
    {
        if (TerminateRequested()) {
            running = false;
        }

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
                // Toggle map with 'M' key
                if (e.key.keysym.sym == SDLK_m)
                {
                    ToggleMap();
                    // When the map is visible, release mouse; recapture when closing.
                    player.SetMouseCaptured(!IsMapVisible());
                }
                // Zoom in with numpad + or KP_PLUS
                if (e.key.keysym.sym == SDLK_KP_PLUS)
                {
                    ZoomMap(Config::UI::Map::ZOOM_STEP);
                }
                // Zoom out with numpad - or KP_MINUS
                if (e.key.keysym.sym == SDLK_KP_MINUS)
                {
                    ZoomMap(-Config::UI::Map::ZOOM_STEP);
                }
                // Toggle debug overlay with tilde/backquote key (`/~)
                if (e.key.keysym.sym == SDLK_BACKQUOTE)
                {
                    ToggleDebugOverlay();
                }
                // Toggle global wireframe mode with 'F'
                if (e.key.keysym.sym == SDLK_f)
                {
                    wireframeMode = !wireframeMode;
                    SetGlobalWireframeMode(wireframeMode);
                }
                // Let player handle other key presses
                player.HandleKeyPress(e.key.keysym.sym);
            }

            // Mouse motion for looking around
            if (e.type == SDL_MOUSEMOTION)
            {
                player.HandleMouseMotion((float)e.motion.xrel, (float)e.motion.yrel);
            }
        }

        // Update player movement and physics
        const Uint8 *kb = SDL_GetKeyboardState(NULL);
        
        // Handle map scrolling when map is visible
        if (IsMapVisible()) {
            glm::vec2 scrollDelta(0.0f, 0.0f);
            if (kb[SDL_SCANCODE_W]) scrollDelta.y -= Config::UI::Map::MAP_SCROLL_SPEED * delta;
            if (kb[SDL_SCANCODE_S]) scrollDelta.y += Config::UI::Map::MAP_SCROLL_SPEED * delta;
            if (kb[SDL_SCANCODE_A]) scrollDelta.x -= Config::UI::Map::MAP_SCROLL_SPEED * delta;
            if (kb[SDL_SCANCODE_D]) scrollDelta.x += Config::UI::Map::MAP_SCROLL_SPEED * delta;
            UpdateMapOffset(scrollDelta);
        } else {
            // Normal player movement when map is not visible
            player.HandleKeyboard(kb, delta, &npcSystem);
        }
        
        player.Update(delta);

        // Deferred passes must not blend into the G-buffer.
        glDisable(GL_BLEND);

        // Update view matrix from player camera
        glm::vec3 cameraPos = player.GetPosition();
        glm::vec3 cameraFront = player.GetFront();
        glm::vec3 cameraUp = player.GetUp();
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

        // Update terrain generation around camera
        UpdateTerrain(cameraPos);

        if (wireframeMode) {
            // Wireframe mode: no deferred lighting/shadows.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, windowW, windowH);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);

            // A brighter background helps readability when we skip the skybox.
            glClearColor(0.62f, 0.74f, 0.92f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Render scene geometry directly with an unlit shader.
            const GLuint wf = wireframeProgram ? wireframeProgram : DeferredRenderer::GetGeometryShader();
            RenderTerrainToGBuffer(wf, proj, view);
            npcSystem.RenderToGBuffer(wf, proj, view);
        } else {
            // Restore default clear color; skybox will cover anyway.
            glClearColor(0.1f, 0.1f, 0.12f, 1.0f);

            // === SHADOW PASS ===
            DeferredRenderer::BeginShadowPass();
            glm::mat4 lightSpaceMatrix = DeferredRenderer::GetLightSpaceMatrix();
            RenderTerrainToShadowMap(DeferredRenderer::GetShadowShader(), lightSpaceMatrix);
            npcSystem.RenderToShadowMap(DeferredRenderer::GetShadowShader(), lightSpaceMatrix);
            DeferredRenderer::EndShadowPass();

            // === GEOMETRY PASS (render to G-buffer) ===
            DeferredRenderer::BeginGeometryPass();

            // Render world geometry to G-buffer
            RenderTerrainToGBuffer(DeferredRenderer::GetGeometryShader(), proj, view);
            npcSystem.RenderToGBuffer(DeferredRenderer::GetGeometryShader(), proj, view);

            DeferredRenderer::EndGeometryPass();

            // === LIGHTING PASS (deferred shading) ===
            // Reset to default framebuffer and viewport
            glViewport(0, 0, windowW, windowH);
            DeferredRenderer::LightingPass(view, proj, cameraPos);

            // Copy depth buffer from G-buffer to default framebuffer for forward rendering
            DeferredRenderer::CopyDepthToDefaultFramebuffer();

            // === SKYBOX PASS (render after lighting but before HUD) ===
            Skybox::Render(view, proj);
        }

        // HUD/UI uses alpha blending.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // === HUD RENDERING (map/minimap - no lighting/shadows) ===
        // These are 2D overlays, so they don't need shadows or lighting
        // Render map UI - either full screen or corner minimap (marker uses program3D, layers use per-type shaders)
        // When map is visible, show full screen map with current offset, otherwise show small corner view
        if (program3D)
        {
            glUseProgram(program3D);

            // Render 3D shapes
            if (program3D)
            {
                angle += delta * 1.0f; // radians per second
            }

            // Render map UI
            if (IsMapVisible()) {
                // Get current map offset for scrolling
                glm::vec2 mapOffset(0.0f, 0.0f);
                // We need to track the offset, so we'll pass it through the state
                // The offset is already tracked internally in map.cpp
                RenderMap(cameraPos, cameraFront, windowW, windowH,
                          mapBorderShader,
                          markerSdfProgram,
                          0,
                          mapHighwaysShader,
                          mapRoadsShader,
                          mapStreetsShader,
                          mapBuildingsShader,
                          true,
                          glm::vec2(0.0f));
            } else {
                RenderMap(cameraPos, cameraFront, windowW, windowH,
                          mapBorderShader,
                          markerSdfProgram,
                          0,
                          mapHighwaysShader,
                          mapRoadsShader,
                          mapStreetsShader,
                          mapBuildingsShader,
                          false,
                          glm::vec2(0.0f));
            }
        }

        // NPC HUD labels (unlit, screen-space)
        npcSystem.RenderNameLabels(player.GetPosition(), proj, view, windowW, windowH);

        // Debug overlay
        RenderDebugOverlay(windowW, windowH, wireframeMode, player.GetPosition(), delta);

        SDL_GL_SwapWindow(window);

        // Cap to ~60 FPS
        if (delta < (1.0f / 60.0f))
            SDL_Delay((Uint32)(((1.0f / 60.0f) - delta) * 1000.0f));
    }

    CleanupTextOverlay();

    // Ensure background loader is joined and LLM resources are freed.
    shutdown_llm();

    CleanupDebugOverlay();

    CleanupTerrain();

    npcSystem.Cleanup();

    // Cleanup map
    CleanupMap();

    // Cleanup deferred renderer
    DeferredRenderer::Cleanup();

    // Cleanup skybox
    Skybox::Cleanup();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Exiting." << std::endl;
    return 0;
}
