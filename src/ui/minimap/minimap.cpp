#include "minimap.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../../world/terrain/terrain.h"

static GLuint g_miniVAO = 0;
static GLuint g_miniVBO = 0;

bool InitMinimap() {
    glGenVertexArrays(1, &g_miniVAO);
    glGenBuffers(1, &g_miniVBO);
    glBindVertexArray(g_miniVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_miniVBO);
    glBufferData(GL_ARRAY_BUFFER, 18 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return true;
}

void RenderMinimap(const glm::vec3& cameraPos, int windowW, int windowH, GLuint shaderProgram) {
    // Save previous viewport
    GLint oldVp[4];
    glGetIntegerv(GL_VIEWPORT, oldVp);

    const int miniSize = 220;
    const int margin = 10;
    int miniX = windowW - miniSize - margin;
    int miniY = windowH - miniSize - margin;

    glEnable(GL_SCISSOR_TEST);
    glScissor(miniX, miniY, miniSize, miniSize);
    GLfloat prevClear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
    glClearColor(0.06f, 0.06f, 0.07f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float worldRadius = 40.0f;
    float cx = cameraPos.x;
    float cz = cameraPos.z;

    glm::mat4 projMini = glm::ortho(-worldRadius, worldRadius, -worldRadius, worldRadius, -1000.0f, 1000.0f);
    glm::vec3 eye(cx, 200.0f, cz);
    glm::vec3 center(cx, 0.0f, cz);
    glm::vec3 up(0.0f, 0.0f, -1.0f);
    glm::mat4 viewMini = glm::lookAt(eye, center, up);

    glViewport(miniX, miniY, miniSize, miniSize);

    // Render terrain top-down
    RenderTerrain(shaderProgram, projMini, viewMini);

    // Player marker as small filled square
    float markSize = worldRadius * 0.02f;
    float hy = SampleTerrainHeight(cx, cz) + 50.0f;
    float s = markSize;
    float squareVerts[18] = {
        cx - s, hy, cz - s,
        cx + s, hy, cz - s,
        cx + s, hy, cz + s,
        cx - s, hy, cz - s,
        cx + s, hy, cz + s,
        cx - s, hy, cz + s
    };

    glBindVertexArray(g_miniVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_miniVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(squareVerts), squareVerts);

    // bind shader and set uniform then draw triangles
    glUseProgram(shaderProgram);
    GLint loc2 = glGetUniformLocation(shaderProgram, "uMVP");
    GLint colorLoc2 = glGetUniformLocation(shaderProgram, "uColor");
    glm::mat4 mvpMini = projMini * viewMini * glm::mat4(1.0f);
    glUniformMatrix4fv(loc2, 1, GL_FALSE, glm::value_ptr(mvpMini));
    glUniform3f(colorLoc2, 1.0f, 0.1f, 0.1f);

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthWasEnabled) glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);

    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);
    glDisable(GL_SCISSOR_TEST);
    glViewport(oldVp[0], oldVp[1], oldVp[2], oldVp[3]);
}

void CleanupMinimap() {
    if (g_miniVBO) glDeleteBuffers(1, &g_miniVBO);
    if (g_miniVAO) glDeleteVertexArrays(1, &g_miniVAO);
    g_miniVBO = 0;
    g_miniVAO = 0;
}
