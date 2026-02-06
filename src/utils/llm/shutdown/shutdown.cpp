#include "shutdown.h"

#include "../internal/state.h"

void shutdown_llm() {
    // Ensure any background load has completed.
    LLMInternal::join_loader_thread();

    auto& s = LLMInternal::shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    LLMInternal::free_state_locked();
}
