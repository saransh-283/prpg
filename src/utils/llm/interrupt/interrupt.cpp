#include "interrupt.h"

#include "../internal/state.h"

void interrupt_llm_generation() {
    LLMInternal::shared().interrupt_requested = true;
}

bool is_llm_interrupt_requested() {
    return LLMInternal::shared().interrupt_requested.load();
}

void clear_llm_interrupt() {
    LLMInternal::shared().interrupt_requested = false;
}
