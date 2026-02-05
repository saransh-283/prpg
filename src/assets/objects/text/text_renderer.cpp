#include "text_renderer.h"
#include <utils/shaders/shader_utils.h>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cctype>

static GLuint textVAO = 0;
static GLuint textVBO = 0;
static GLuint textShader = 0;

// Simple 5x7 bitmap font patterns
BitmapChar GetCharacterBitmap(char c) {
    BitmapChar bitmap;
    bitmap.width = 5;
    bitmap.height = 7;
    bitmap.pixels.resize(35, false);
    
    switch(c) {
        case 'A':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,1,
                1,1,1,1,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,0,0,0,0
            };
            break;
        case 'B':
            bitmap.pixels = {
                1,1,1,1,0,
                1,0,0,0,1,
                1,1,1,1,0,
                1,1,1,1,0,
                1,0,0,0,1,
                1,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'C':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,0,
                1,0,0,0,0,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'D':
            bitmap.pixels = {
                1,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'E':
            bitmap.pixels = {
                1,1,1,1,1,
                1,0,0,0,0,
                1,1,1,1,0,
                1,1,1,1,0,
                1,0,0,0,0,
                1,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case 'F':
            bitmap.pixels = {
                1,1,1,1,1,
                1,0,0,0,0,
                1,1,1,1,0,
                1,1,1,1,0,
                1,0,0,0,0,
                1,0,0,0,0,
                0,0,0,0,0
            };
            break;
        case 'G':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,0,
                1,0,1,1,1,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'H':
            bitmap.pixels = {
                1,0,0,0,1,
                1,0,0,0,1,
                1,1,1,1,1,
                1,1,1,1,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,0,0,0,0
            };
            break;
        case 'I':
            bitmap.pixels = {
                1,1,1,1,1,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                1,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case 'J':
            bitmap.pixels = {
                0,0,0,0,1,
                0,0,0,0,1,
                0,0,0,0,1,
                0,0,0,0,1,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'K':
            bitmap.pixels = {
                1,0,0,0,1,
                1,0,0,1,0,
                1,0,1,0,0,
                1,1,0,0,0,
                1,0,1,0,0,
                1,0,0,1,1,
                0,0,0,0,0
            };
            break;
        case 'L':
            bitmap.pixels = {
                1,0,0,0,0,
                1,0,0,0,0,
                1,0,0,0,0,
                1,0,0,0,0,
                1,0,0,0,0,
                1,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case 'M':
            bitmap.pixels = {
                1,0,0,0,1,
                1,1,0,1,1,
                1,0,1,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,0,0,0,0
            };
            break;
        case 'N':
            bitmap.pixels = {
                1,0,0,0,1,
                1,1,0,0,1,
                1,0,1,0,1,
                1,0,0,1,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,0,0,0,0
            };
            break;
        case 'O':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'P':
            bitmap.pixels = {
                1,1,1,1,0,
                1,0,0,0,1,
                1,1,1,1,0,
                1,0,0,0,0,
                1,0,0,0,0,
                1,0,0,0,0,
                0,0,0,0,0
            };
            break;
        case 'Q':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,1,0,1,
                1,0,0,1,1,
                0,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case 'R':
            bitmap.pixels = {
                1,1,1,1,0,
                1,0,0,0,1,
                1,1,1,1,0,
                1,1,1,0,0,
                1,0,1,1,0,
                1,0,0,1,1,
                0,0,0,0,0
            };
            break;
        case 'S':
            bitmap.pixels = {
                0,1,1,1,1,
                1,0,0,0,0,
                0,1,1,1,0,
                0,0,0,0,1,
                0,0,0,0,1,
                1,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'T':
            bitmap.pixels = {
                1,1,1,1,1,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,0,0,0
            };
            break;
        case 'U':
            bitmap.pixels = {
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case 'V':
            bitmap.pixels = {
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                0,1,0,1,0,
                0,1,0,1,0,
                0,0,1,0,0,
                0,0,0,0,0
            };
            break;
        case 'W':
            bitmap.pixels = {
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,0,0,1,
                1,0,1,0,1,
                1,1,0,1,1,
                1,0,0,0,1,
                0,0,0,0,0
            };
            break;
        case 'X':
            bitmap.pixels = {
                1,0,0,0,1,
                0,1,0,1,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,1,0,1,0,
                1,0,0,0,1,
                0,0,0,0,0
            };
            break;
        case 'Y':
            bitmap.pixels = {
                1,0,0,0,1,
                1,0,0,0,1,
                0,1,0,1,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,0,0,0
            };
            break;
        case 'Z':
            bitmap.pixels = {
                1,1,1,1,1,
                0,0,0,1,0,
                0,0,1,0,0,
                0,1,0,0,0,
                1,0,0,0,0,
                1,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case '0':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,1,1,
                1,0,1,0,1,
                1,1,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case '1':
            bitmap.pixels = {
                0,0,1,0,0,
                0,1,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                0,0,1,0,0,
                1,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case '2':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                0,0,0,1,0,
                0,0,1,0,0,
                0,1,0,0,0,
                1,1,1,1,1,
                0,0,0,0,0
            };
            break;
        case '3':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                0,0,1,1,0,
                0,0,1,1,0,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case '4':
            bitmap.pixels = {
                0,0,0,1,0,
                0,0,1,1,0,
                0,1,0,1,0,
                1,0,0,1,0,
                1,1,1,1,1,
                0,0,0,1,0,
                0,0,0,0,0
            };
            break;
        case '5':
            bitmap.pixels = {
                1,1,1,1,1,
                1,0,0,0,0,
                1,1,1,1,0,
                0,0,0,0,1,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case '6':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,0,
                1,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case '7':
            bitmap.pixels = {
                1,1,1,1,1,
                0,0,0,0,1,
                0,0,0,1,0,
                0,0,1,0,0,
                0,1,0,0,0,
                1,0,0,0,0,
                0,0,0,0,0
            };
            break;
        case '8':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                0,1,1,1,0,
                0,1,1,1,0,
                1,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        case '9':
            bitmap.pixels = {
                0,1,1,1,0,
                1,0,0,0,1,
                1,0,0,0,1,
                0,1,1,1,1,
                0,0,0,0,1,
                0,1,1,1,0,
                0,0,0,0,0
            };
            break;
        default:
            // Space or unknown character
            bitmap.pixels.assign(35, false);
            break;
    }
    
    return bitmap;
}

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
        void main() {
            FragColor = vec4(1.0, 1.0, 1.0, 1.0); // White text
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
    
    glUseProgram(textShader);
    glUniform2f(glGetUniformLocation(textShader, "screenSize"), (float)windowWidth, (float)windowHeight);
    
    glBindVertexArray(textVAO);
    
    // Character sizing
    float pixelSize = 2.0f;  // Size of each pixel in the bitmap
    float charSpacing = 7.0f * pixelSize;  // Space between characters
    
    float currentX = x;
    
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
                    
                    glUniform2f(glGetUniformLocation(textShader, "position"), pixelX, pixelY);
                    
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
