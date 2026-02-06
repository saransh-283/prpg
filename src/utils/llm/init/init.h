#pragma once

#include <string>

// A simple RAII-like wrapper for the llama model and context used in the project.
// The implementation lives in init.cpp (loading/generation), plus:
// - shutdown/shutdown.cpp (shutdown)
// - interrupt/interrupt.cpp (interrupt)

enum class LLMModelState {
    Uninitialized,
    Loading,
    Ready,
    Generating,
    Error,
};

bool initialize_llm(const std::string& model_path, int ngl = 99, int n_ctx = 2048);
bool start_llm_background_load(const std::string& model_path, int ngl = 99, int n_ctx = 2048);

bool is_llm_loading();
bool is_llm_ready();
bool is_llm_generating();

LLMModelState llm_get_state();

std::string llm_last_error();
void wait_for_llm_load();

// Clears chat history and context (KV/memory cache) but keeps the model loaded.
void clear_llm_context();

std::string generate_from_prompt(const std::string& prompt);
