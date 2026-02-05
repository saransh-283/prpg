#include "text_renderer.h"
#include <utils/shaders/shader_utils.h>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

static GLuint textVAO = 0;
static GLuint textVBO = 0;
static GLuint textShader = 0;

void InitTextOverlay() {
    // Simple text shader for 2D overlay
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        uniform vec2 screenSize;
        uniform vec2 position;
        void main() {
            vec2 normalizedPos = ((aPos + position) / screenSize) * 2.0 - 1.0;
            normalizedPos.y = -normalizedPos.y; // Flip Y coordinate
            gl_Position = vec4(normalizedPos, 0.0, 1.0);
        }
    )";
    
    const char* fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 uColor;
        void main() {
            FragColor = uColor;
        }
    )";
    
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    
    textShader = glCreateProgram();
    glAttachShader(textShader, vertexShader);
    glAttachShader(textShader, fragmentShader);
    glLinkProgram(textShader);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    // Create VAO and VBO for text rendering
    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);
    
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void RenderTextOverlay(const std::string& text, float x, float y, int windowWidth, int windowHeight) {
    if (textShader == 0) return;
    
    // Convert text to uppercase
    std::string upperText = text;
    std::transform(upperText.begin(), upperText.end(), upperText.begin(), ::toupper);
    
    // Disable depth testing for overlay
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    
    glUseProgram(textShader);
    const GLint screenSizeLoc = glGetUniformLocation(textShader, "screenSize");
    const GLint positionLoc = glGetUniformLocation(textShader, "position");
    const GLint colorLoc = glGetUniformLocation(textShader, "uColor");

    if (screenSizeLoc >= 0) glUniform2f(screenSizeLoc, (float)windowWidth, (float)windowHeight);
    
    glBindVertexArray(textVAO);
    
    // Character sizing
    float pixelSize = 2.0f;  // Size of each pixel in the bitmap
    float charSpacing = 7.0f * pixelSize;  // Space between characters

    // --- background for readability ---
    // Measure approximate bounds based on the fixed advance used by the renderer.
    int maxBitmapHeight = 0;
    for (char c : upperText) {
        BitmapChar bitmap = GetCharacterBitmap(c);
        maxBitmapHeight = std::max(maxBitmapHeight, bitmap.height);
    }

    const float textW = (float)upperText.size() * charSpacing;
    const float textH = (float)maxBitmapHeight * pixelSize;
    const float pad = 4.0f;

    if (textW > 0.0f && textH > 0.0f) {
        const float bgW = textW + pad * 2.0f;
        const float bgH = textH + pad * 2.0f;

        float bgVertices[] = {
            0.0f,  0.0f,
            bgW,   0.0f,
            bgW,   bgH,

            0.0f,  0.0f,
            bgW,   bgH,
            0.0f,  bgH
        };

        if (colorLoc >= 0) glUniform4f(colorLoc, 0.0f, 0.0f, 0.0f, 0.55f);
        if (positionLoc >= 0) glUniform2f(positionLoc, x - pad, y - pad);

        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bgVertices), bgVertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    
    float currentX = x;

    if (colorLoc >= 0) glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    
    for (char c : upperText) {
        BitmapChar bitmap = GetCharacterBitmap(c);
        
        // Render each pixel of the character
        for (int row = 0; row < bitmap.height; ++row) {
            for (int col = 0; col < bitmap.width; ++col) {
                if (bitmap.pixels[row * bitmap.width + col]) {
                    float pixelX = currentX + col * pixelSize;
                    float pixelY = y + row * pixelSize;
                    
                    // Create a small rectangle for this pixel
                    float vertices[] = {
                        0.0f,      0.0f,        // Bottom-left
                        pixelSize, 0.0f,        // Bottom-right  
                        pixelSize, pixelSize,   // Top-right
                        
                        0.0f,      0.0f,        // Bottom-left
                        pixelSize, pixelSize,   // Top-right
                        0.0f,      pixelSize    // Top-left
                    };
                    
                    if (positionLoc >= 0) glUniform2f(positionLoc, pixelX, pixelY);
                    
                    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
            }
        }
        
        currentX += charSpacing;
    }
    
    glBindVertexArray(0);
    
    // Re-enable depth testing
    glEnable(GL_DEPTH_TEST);
}

void CleanupTextOverlay() {
    if (textVAO) glDeleteVertexArrays(1, &textVAO);
    if (textVBO) glDeleteBuffers(1, &textVBO);
    if (textShader) glDeleteProgram(textShader);
}
