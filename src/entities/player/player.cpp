#include "player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <world/terrain/terrain.h>
#include <iostream>

Player::Player()
    : cameraPos(0.0f, 0.0f, 3.0f)
    , cameraFront(0.0f, 0.0f, -1.0f)
    , cameraUp(0.0f, 1.0f, 0.0f)
    , yaw(-90.0f)
    , pitch(0.0f)
    , velocity(0.0f)
    , onGround(false)
    , flying(false)
    , mouseCaptured(true)
{
}

void Player::Initialize(const glm::vec3& initialPosition)
{
    cameraPos = initialPosition;
    velocity = glm::vec3(0.0f);
    onGround = false;
    
    // Set mouse capture mode
    SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
}

void Player::Update(float deltaTime)
{
    if (!flying) {
        ApplyGravity(deltaTime);
        CheckGroundCollision();
    }
}

void Player::HandleKeyboard(const Uint8* keyboardState, float deltaTime)
{
    // Calculate horizontal forward direction (ignore Y component for ground movement)
    glm::vec3 horizontalFront = glm::normalize(glm::vec3(cameraFront.x, 0.0f, cameraFront.z));
    
    // Handle edge case where looking straight up/down (horizontal component is zero)
    if (glm::length(horizontalFront) < 0.001f) {
        // Fall back to yaw-only direction when looking straight up/down
        horizontalFront = glm::vec3(cos(glm::radians(yaw)), 0.0f, sin(glm::radians(yaw)));
    }
    
    // WASD movement using horizontal direction
    if (keyboardState[SDL_SCANCODE_W]) {
        cameraPos += horizontalFront * moveSpeed * deltaTime;
    }
    if (keyboardState[SDL_SCANCODE_S]) {
        cameraPos -= horizontalFront * moveSpeed * deltaTime;
    }
    
    // Right vector for strafing (also horizontal)
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    if (keyboardState[SDL_SCANCODE_A]) {
        cameraPos -= right * moveSpeed * deltaTime;
    }
    if (keyboardState[SDL_SCANCODE_D]) {
        cameraPos += right * moveSpeed * deltaTime;
    }
    
    // Flying vertical control: Up/Down when flying
    if (flying) {
        if (keyboardState[SDL_SCANCODE_UP]) {
            cameraPos.y += flySpeed * deltaTime;
        }
        if (keyboardState[SDL_SCANCODE_DOWN]) {
            cameraPos.y -= flySpeed * deltaTime;
        }
    }
}

void Player::HandleMouseMotion(float xrel, float yrel)
{
    if (!mouseCaptured) return;
    
    yaw += xrel * mouseSensitivity;
    pitch -= yrel * mouseSensitivity; // invert Y
    
    // Constrain pitch
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;
        
    UpdateCameraFront();
}

void Player::HandleKeyPress(SDL_Keycode key)
{
    switch (key) {
        case SDLK_m:
            // Toggle mouse capture
            SetMouseCaptured(!mouseCaptured);
            break;
            
        case SDLK_SPACE:
            // Toggle flying mode
            flying = !flying;
            if (flying) {
                // Stop any falling motion when entering fly
                velocity.y = 0.0f;
                onGround = false;
            } else {
                // When disabling fly, ensure we're not below ground
                float groundY = SampleTerrainHeight(cameraPos.x, cameraPos.z) + 0.5f;
                if (cameraPos.y < groundY)
                    cameraPos.y = groundY;
            }
            break;
    }
}

void Player::SetMouseCaptured(bool captured)
{
    mouseCaptured = captured;
    SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
}

void Player::UpdateCameraFront()
{
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void Player::ApplyGravity(float deltaTime)
{
    velocity.y += gravity * deltaTime;
    cameraPos += velocity * deltaTime;
}

void Player::CheckGroundCollision()
{
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
