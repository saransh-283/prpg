#pragma once

#include <core/config.h>
#include <glm/glm.hpp>
#include <SDL2/SDL.h>

class NpcSystem;

class Player {
public:
    Player();
    ~Player() = default;

    // Initialize player at given position
    void Initialize(const glm::vec3& initialPosition);

    // Update player state
    void Update(float deltaTime);

    // Handle input events
    void HandleKeyboard(const Uint8* keyboardState, float deltaTime, const NpcSystem* npcSystem = nullptr);
    void HandleMouseMotion(float xrel, float yrel);
    void HandleKeyPress(SDL_Keycode key);

    // Getters
    glm::vec3 GetPosition() const { return cameraPos; }
    glm::vec3 GetFront() const { return cameraFront; }
    glm::vec3 GetUp() const { return cameraUp; }
    glm::vec3 GetVelocity() const { return velocity; }
    
    float GetYaw() const { return yaw; }
    float GetPitch() const { return pitch; }
    
    bool IsOnGround() const { return onGround; }
    bool IsMouseCaptured() const { return mouseCaptured; }

    // Setters
    void SetPosition(const glm::vec3& position) { cameraPos = position; }
    void SetMouseCaptured(bool captured);

private:
    // Camera/position state
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    
    // Camera orientation
    float yaw;   // degrees, -Z
    float pitch; // degrees
    
    // Physics state
    glm::vec3 velocity;
    bool onGround;
    
    // Input state
    bool mouseCaptured;
    
    // Movement parameters
    static constexpr float mouseSensitivity = Config::Player::MOUSE_SENSITIVITY;
    static constexpr float moveSpeed = Config::Player::MOVE_SPEED; // units per second
    static constexpr float gravity = Config::Player::GRAVITY;
    
    // Helper methods
    void UpdateCameraFront();
    void ApplyGravity(float deltaTime);
    void CheckGroundCollision();
};
