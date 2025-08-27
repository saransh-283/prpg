#include "loading.h"
#include <vector>
#include <queue>
#include <functional>
#include <string>
#include <mutex>
#include <cstdio>
#include <glad/glad.h>
#include <assets/objects/text/text_renderer.h>
#include <glm/glm.hpp>
#include <iostream>

static std::queue<std::pair<std::function<bool()>, std::string>> g_tasks;
static int g_totalTasks = 0;
static int g_doneTasks = 0;
static bool g_inited = false;
static float g_spinnerAngle = 0.0f;

void InitLoading() {
    g_totalTasks = 0;
    g_doneTasks = 0;
    while (!g_tasks.empty()) g_tasks.pop();
    g_inited = true;
    g_spinnerAngle = 0.0f;
}

void CleanupLoading() {
    // nothing to free; tasks cleared on init
}

void AddLoadingTask(const std::function<bool()>& task, const std::string& description) {
    g_tasks.push({task, description});
    ++g_totalTasks;
}

void ClearLoadingTasks() {
    while (!g_tasks.empty()) g_tasks.pop();
    // Account for the currently running task: set total to done+1 so progress
    // doesn't exceed 100% when a task calls ClearLoadingTasks() while still
    // executing (we increment g_doneTasks after task returns).
    g_totalTasks = g_doneTasks + 1;
}

void UpdateLoading() {
    if (!g_inited) return;
    if (!g_tasks.empty()) {
        auto taskPair = g_tasks.front();
        std::string desc = taskPair.second;
        // pop before execution to avoid assertion if the task clears the queue
        g_tasks.pop();
        // execute one task per frame to keep UI responsive and valid GL context
        bool ok = taskPair.first();
        (void)ok; // we don't abort on failure for now
        ++g_doneTasks;

        // Log completion of this task with progress
        float prog = GetLoadingProgress();
    int percent = (int)(prog * 100.0f);
    if (percent > 100) percent = 100;
    std::cout << "[Loader] Completed: " << desc << " (" << percent << "% )" << std::endl;
    }
    // advance spinner
    g_spinnerAngle += 0.12f;
    if (g_spinnerAngle > 1000000.0f) g_spinnerAngle = 0.0f;
}

bool IsLoadingDone() {
    return g_inited && g_tasks.empty();
}

float GetLoadingProgress() {
    if (g_totalTasks == 0) return 1.0f;
    return (float)g_doneTasks / (float)g_totalTasks;
}

void RenderLoading(int windowW, int windowH) {
    // dark translucent backdrop
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    // simple progress text in center + spinner made of characters
    std::string title = "Loading...";
    float px = (float)(windowW / 2 - 8 * 10); // approximate centering
    float py = (float)(windowH / 2 - 20);
    RenderTextOverlay(title, px, py - 20.0f, windowW, windowH);

    // progress bar text
    char buf[128];
    float prog = GetLoadingProgress();
    int percent = (int)(prog * 100.0f);
    snprintf(buf, sizeof(buf), "Progress: %d%%", percent);
    RenderTextOverlay(buf, px, py + 4.0f, windowW, windowH);

    // spinner - use simple rotating line made of characters
    const char spinnerChars[] = {'|', '/', '-', '\\'};
    int idx = (int)(g_spinnerAngle) % 4;
    char s[2] = { spinnerChars[idx], '\0' };
    std::string spinStr = std::string(" ") + s;
    RenderTextOverlay(spinStr, px + 120.0f, py + 4.0f, windowW, windowH);

    glEnable(GL_DEPTH_TEST);
}
