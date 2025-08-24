#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <glad/glad.h>
#include <string>
#include <vector>

struct BitmapChar {
    std::vector<bool> pixels;
    int width;
    int height;
};

void RenderTextOverlay(const std::string& text, float x, float y, int windowWidth, int windowHeight);
void InitTextOverlay();
void CleanupTextOverlay();
BitmapChar GetCharacterBitmap(char c);

#endif // TEXT_RENDERER_H
