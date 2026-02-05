#pragma once

#include <string>
#include <llama.h>

// A simple RAII wrapper for the llama model and context used in the project.
// The implementation lives in llm.cpp. This header exposes a minimal API:
// - initialize_llm(model_path, ngl, n_ctx)
// - start_llm_background_load(model_path, ngl, n_ctx)
// - generate_from_prompt(prompt) -> string
// - shutdown_llm()

bool initialize_llm(const std::string& model_path, int ngl = 99, int n_ctx = 2048);
bool start_llm_background_load(const std::string& model_path, int ngl = 99, int n_ctx = 2048);

bool is_llm_loading();
bool is_llm_ready();
std::string llm_last_error();
void wait_for_llm_load();

std::string generate_from_prompt(const std::string& prompt);
void shutdown_llm();
