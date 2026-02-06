#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <llama.h>

namespace LLMInternal {

struct Shared {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    llama_sampler* sampler_basic = nullptr;

    std::mutex mutex;
    std::mutex thread_mutex;

    std::thread loader_thread;

    std::atomic<bool> loading{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> generating{false};
    std::atomic<bool> interrupt_requested{false};

    std::string last_error;
};

Shared& shared();

void set_last_error(const std::string& msg);
void clear_last_error();
std::string get_last_error();

// Must be called with shared().mutex held.
void free_state_locked();

// Joins the background loader thread if needed.
void join_loader_thread();

} // namespace LLMInternal
