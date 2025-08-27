#pragma once

#include <functional>
#include <string>

// Initialize and cleanup the loading system
void InitLoading();
void CleanupLoading();

// Add a loading task. The task should perform a small chunk of initialization
// work and return true when the task completed successfully (or false on error
// but the loader will continue). Tasks are executed one-per-update on the main
// thread so GL calls remain valid.
void AddLoadingTask(const std::function<bool()>& task, const std::string& description = "");

// Run one loading step (called from the main loop while showing the loader).
void UpdateLoading();

// Query loader state
bool IsLoadingDone();
float GetLoadingProgress(); // 0.0 .. 1.0

// Render a simple loading overlay using the project's text renderer.
void RenderLoading(int windowW, int windowH);

// Cancel any remaining queued loading tasks (used to early-exit loader)
void ClearLoadingTasks();
