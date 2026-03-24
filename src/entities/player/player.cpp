#include "player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <world/terrain/terrain.h>
#include <entities/npc/npc.h>
#include <iostream>
#include <limits>
#include <cmath>

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
    const auto& p = CoreParams::GetPlayerParams();
    mouseSensitivity = p.value("mouse_sensitivity", 0.1f);
    moveSpeed = p.value("move_speed", 5.0f);
    gravity = p.value("gravity", -9.8f);
    jumpVelocity = p.value("jump_velocity", 5.5f);
    headClearance = p.value("head_clearance", 0.12f);
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

void Player::HandleKeyboard(const Uint8* keyboardState, float deltaTime, const NpcSystem* npcSystem)
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

    const auto& p = CoreParams::GetPlayerParams();
    const float r = static_cast<float>(p.value("collision_radius", 0.5f));
    const float eyeH = static_cast<float>(p.value("eye_height", 1.6f));
    glm::vec3 newPos = cameraPos;

    auto collides = [&](float x, float z) {
        // Height-aware building collision so doorway openings work:
        // collide only against wall triangles that overlap the player's vertical span.
        const float feetY = cameraPos.y - eyeH;
        const float headY = cameraPos.y + std::max(0.02f, headClearance);
        if (CollidesWithBuilding(x, z, r, feetY, headY)) return true;
        if (npcSystem && npcSystem->CollidesXZ(x, z, r)) return true;
        return false;
    };

    // Slide-style resolution: try X then Z.
    glm::vec3 tryX = glm::vec3(cameraPos.x + delta.x, cameraPos.y, cameraPos.z);
    if (!collides(tryX.x, tryX.z)) {
        newPos.x = tryX.x;
    }

    glm::vec3 tryZ = glm::vec3(newPos.x, cameraPos.y, cameraPos.z + delta.z);
    if (!collides(tryZ.x, tryZ.z)) {
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
    if (key == SDLK_SPACE) {
        if (onGround) {
            velocity.y = std::max(0.0f, jumpVelocity);
            onGround = false;
        }
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
    const auto& p = CoreParams::GetPlayerParams();
    const float eyeH = static_cast<float>(p.value("eye_height", 1.6f));
    const float feetY = cameraPos.y - eyeH;

    float floorY = 0.0f;
    float ceilingY = std::numeric_limits<float>::infinity();
    (void)SampleWalkableFloorAndCeiling(cameraPos.x, cameraPos.z, feetY, floorY, ceilingY);

    // Reject spurious "ceiling" hits that are effectively the current floor plane.
    // With mesh sampling, tiny penetration can otherwise classify the same surface as both
    // floor and ceiling and force the camera downward.
    const float minStandingHeadroom = eyeH + std::max(0.02f, headClearance);
    if (std::isfinite(ceilingY) && ceilingY <= floorY + minStandingHeadroom) {
        ceilingY = std::numeric_limits<float>::infinity();
    }

    // Ground snap.
    const float desiredCamY = floorY + eyeH;
    if (cameraPos.y <= desiredCamY) {
        cameraPos.y = desiredCamY;
        if (velocity.y < 0.0f) velocity.y = 0.0f;
        onGround = true;
    } else {
        onGround = false;
    }

    // Ceiling collision (prevents jumping through upper floors / roof).
    if (std::isfinite(ceilingY)) {
        const float maxCamY = ceilingY - std::max(0.02f, headClearance);
        if (cameraPos.y > maxCamY) {
            cameraPos.y = maxCamY;
            if (velocity.y > 0.0f) velocity.y = 0.0f;
        }
    }
}
