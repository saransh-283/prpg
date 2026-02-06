#include "state.h"

namespace LLMInternal {

Shared& shared() {
    static Shared s;
    return s;
}

void set_last_error(const std::string& msg) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.last_error = msg;
}

void clear_last_error() {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.last_error.clear();
}

std::string get_last_error() {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.last_error;
}

void free_state_locked() {
    Shared& s = shared();

    if (s.sampler_basic) {
        llama_sampler_free(s.sampler_basic);
        s.sampler_basic = nullptr;
    }

    if (s.ctx) {
        llama_free(s.ctx);
        s.ctx = nullptr;
    }

    if (s.model) {
        llama_model_free(s.model);
        s.model = nullptr;
    }

    s.vocab = nullptr;

    s.ready = false;
    s.generating = false;
    s.interrupt_requested = false;
}

void join_loader_thread() {
    Shared& s = shared();

    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(s.thread_mutex);
        if (s.loader_thread.joinable()) {
            to_join = std::move(s.loader_thread);
        }
    }

    if (to_join.joinable()) {
        to_join.join();
    }

    // If the thread finished, loading may already be false; enforce it.
    s.loading = false;
}

} // namespace LLMInternal
