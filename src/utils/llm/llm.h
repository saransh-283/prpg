#pragma once

#include <string>
#include <llama.h>

// A simple RAII wrapper for the llama model and context used in the project.
// The implementation lives in llm.cpp. This header exposes a minimal API:
// - initialize_llm(model_path, ngl, n_ctx)
// - generate_from_prompt(prompt) -> string
// - shutdown_llm()

bool initialize_llm(const std::string& model_path, int ngl = 99, int n_ctx = 2048);
std::string generate_from_prompt(const std::string& prompt);
void shutdown_llm();
