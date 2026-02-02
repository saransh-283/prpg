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
    ApplyGravity(deltaTime);
    CheckGroundCollision();
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
    
    // Build movement delta first, then resolve collisions.
    glm::vec3 delta(0.0f);

    if (keyboardState[SDL_SCANCODE_W]) delta += horizontalFront;
    if (keyboardState[SDL_SCANCODE_S]) delta -= horizontalFront;

    // Right vector for strafing (also horizontal)
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    if (keyboardState[SDL_SCANCODE_A]) delta -= right;
    if (keyboardState[SDL_SCANCODE_D]) delta += right;

    if (glm::length(delta) < 0.0001f) return;
    delta = glm::normalize(glm::vec3(delta.x, 0.0f, delta.z)) * moveSpeed * deltaTime;

    const float r = Config::Player::COLLISION_RADIUS;
    glm::vec3 newPos = cameraPos;

    // Slide-style resolution: try X then Z.
    glm::vec3 tryX = glm::vec3(cameraPos.x + delta.x, cameraPos.y, cameraPos.z);
    if (!CollidesWithBuilding(tryX.x, tryX.z, r)) {
        newPos.x = tryX.x;
    }

    glm::vec3 tryZ = glm::vec3(newPos.x, cameraPos.y, cameraPos.z + delta.z);
    if (!CollidesWithBuilding(tryZ.x, tryZ.z, r)) {
        newPos.z = tryZ.z;
    }

    cameraPos = newPos;
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
