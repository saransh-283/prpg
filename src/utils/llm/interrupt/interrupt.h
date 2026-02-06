#pragma once

// Requests the current (or next) generation to stop early.
// Generation will return the partial response collected so far.
void interrupt_llm_generation();

// Returns whether an interrupt has been requested.
bool is_llm_interrupt_requested();

// Clears any pending interrupt request.
void clear_llm_interrupt();
