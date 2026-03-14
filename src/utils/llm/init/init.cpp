#include "init.h"

#include <core/params/params.h>

#include "../internal/state.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static bool load_state(const std::string& model_path,
                       int ngl,
                       int n_ctx,
                       llama_model*& out_model,
                       llama_context*& out_ctx,
                       const llama_vocab*& out_vocab,
                       llama_sampler*& out_sampler,
                       std::string& out_error) {
    out_model = nullptr;
    out_ctx = nullptr;
    out_vocab = nullptr;
    out_sampler = nullptr;
    out_error.clear();

    // Load dynamic backends
    ggml_backend_load_all();

    // --- load model ---
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;

    out_model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!out_model) {
        out_error = std::string("Failed to load model: ") + model_path;
        return false;
    }

    out_vocab = llama_model_get_vocab(out_model);

    // initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;
    const auto& llm = CoreParams::GetLLMParams();
    ctx_params.n_batch = static_cast<int>(llm.value("batch_size", 8192));
    ctx_params.n_ubatch = static_cast<int>(llm.value("micro_batch_size", 512));
    ctx_params.n_threads = static_cast<int>(llm.value("cpu_threads", 16));
    ctx_params.n_threads_batch = static_cast<int>(llm.value("batch_threads", 16));

    out_ctx = llama_init_from_model(out_model, ctx_params);
    if (!out_ctx) {
        llama_model_free(out_model);
        out_model = nullptr;
        out_error = "Failed to create context";
        return false;
    }

    // initialize the sampler
    out_sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(out_sampler, llama_sampler_init_min_p(static_cast<float>(llm.value("min_p", 0.05)), 1));
    llama_sampler_chain_add(out_sampler, llama_sampler_init_temp(static_cast<float>(llm.value("temperature", 0.8))));
    llama_sampler_chain_add(out_sampler, llama_sampler_init_dist(static_cast<int>(llm.value("default_seed", 0))));

    return true;
}

static std::string format_chat(const std::string& user_prompt) {
    // NOTE: This is the chat template for Meta-Llama-3-Instruct.
    std::string templated =
        "<|start_header_id|>system<|end_header_id|>\n\n"
        "Cutting Knowledge Date: December 2023\n"
        "Today Date: 23 July 2024\n\n"
        "You are a helpful assistant<|eot_id|><|start_header_id|>user<|end_header_id|>\n\n" +
        user_prompt + "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n";
    return templated;
}

bool initialize_llm(const std::string& model_path, int ngl, int n_ctx) {
    wait_for_llm_load();
    LLMInternal::clear_last_error();

    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    llama_sampler* sampler = nullptr;
    std::string err;

    const bool ok = load_state(model_path, ngl, n_ctx, model, ctx, vocab, sampler, err);
    if (!ok) {
        LLMInternal::set_last_error(err);
        std::cerr << err << "\n";
        return false;
    }

    auto& s = LLMInternal::shared();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        LLMInternal::free_state_locked();
        s.model = model;
        s.ctx = ctx;
        s.vocab = vocab;
        s.sampler_basic = sampler;
        s.ready = true;
    }

    s.loading = false;
    return true;
}

bool start_llm_background_load(const std::string& model_path, int ngl, int n_ctx) {
    auto& s = LLMInternal::shared();

    if (s.ready.load()) return false;

    // If a previous loader thread finished but wasn't joined yet, join it now.
    LLMInternal::join_loader_thread();

    bool expected = false;
    if (!s.loading.compare_exchange_strong(expected, true)) {
        // already loading
        return false;
    }

    LLMInternal::clear_last_error();

    {
        std::lock_guard<std::mutex> lock(s.thread_mutex);
        s.loader_thread = std::thread([model_path, ngl, n_ctx]() {
            llama_model* model = nullptr;
            llama_context* ctx = nullptr;
            const llama_vocab* vocab = nullptr;
            llama_sampler* sampler = nullptr;
            std::string err;

            const bool ok = load_state(model_path, ngl, n_ctx, model, ctx, vocab, sampler, err);
            if (!ok) {
                LLMInternal::set_last_error(err);
                std::cerr << "[LLM] " << err << "\n";
                auto& s2 = LLMInternal::shared();
                s2.ready = false;
                s2.loading = false;
                return;
            }

            {
                auto& s2 = LLMInternal::shared();
                std::lock_guard<std::mutex> lock2(s2.mutex);
                LLMInternal::free_state_locked();
                s2.model = model;
                s2.ctx = ctx;
                s2.vocab = vocab;
                s2.sampler_basic = sampler;
                s2.ready = true;
            }

            auto& s2 = LLMInternal::shared();
            s2.loading = false;
            std::cout << "[LLM] Model loaded in background." << std::endl;
        });
    }

    return true;
}

bool is_llm_loading() {
    return LLMInternal::shared().loading.load();
}

bool is_llm_ready() {
    return LLMInternal::shared().ready.load();
}

bool is_llm_generating() {
    return LLMInternal::shared().generating.load();
}

LLMModelState llm_get_state() {
    auto& s = LLMInternal::shared();

    if (s.generating.load()) return LLMModelState::Generating;
    if (s.loading.load()) return LLMModelState::Loading;
    if (s.ready.load()) return LLMModelState::Ready;

    if (!LLMInternal::get_last_error().empty()) return LLMModelState::Error;
    return LLMModelState::Uninitialized;
}

std::string llm_last_error() {
    return LLMInternal::get_last_error();
}

void wait_for_llm_load() {
    LLMInternal::join_loader_thread();
}

void clear_llm_context() {
    // Avoid clearing while a background load is in progress.
    wait_for_llm_load();

    auto& s = LLMInternal::shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.ctx) return;

    llama_memory_clear(llama_get_memory(s.ctx), true);
}

std::string generate_from_prompt(const std::string& prompt) {
    auto& s = LLMInternal::shared();
    std::lock_guard<std::mutex> lock(s.mutex);

    if (!s.ctx || !s.model || !s.vocab || !s.sampler_basic) return "";

    struct GeneratingGuard {
        std::atomic<bool>& flag;
        explicit GeneratingGuard(std::atomic<bool>& f) : flag(f) { flag = true; }
        ~GeneratingGuard() { flag = false; }
    } generating_guard(s.generating);

    std::string formatted = format_chat(prompt);
    std::string response;

    const bool is_first = llama_memory_seq_pos_max(llama_get_memory(s.ctx), 0) == -1;

    // tokenize the prompt
    const int n_prompt_tokens =
        -llama_tokenize(s.vocab, formatted.c_str(), formatted.size(), nullptr, 0, is_first, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    if (llama_tokenize(s.vocab,
                       formatted.c_str(),
                       formatted.size(),
                       prompt_tokens.data(),
                       prompt_tokens.size(),
                       is_first,
                       true) < 0) {
        GGML_ABORT("failed to tokenize the prompt\n");
    }

    // prepare a batch for the prompt
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    llama_token new_token_id;

    while (true) {
        if (s.interrupt_requested.load()) {
            // Consume the interrupt request so it doesn't affect the next generation.
            s.interrupt_requested = false;
            break;
        }

        // check if we have enough space in the context to evaluate this batch
        int n_ctx = llama_n_ctx(s.ctx);
        int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(s.ctx), 0) + 1;
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            fprintf(stderr, "context size exceeded\n");
            break;
        }

        int ret = llama_decode(s.ctx, batch);
        if (ret != 0) {
            GGML_ABORT("failed to decode, ret = %d\n", ret);
        }

        if (s.interrupt_requested.load()) {
            s.interrupt_requested = false;
            break;
        }

        // sample the next token
        new_token_id = llama_sampler_sample(s.sampler_basic, s.ctx, -1);

        // is it an end of generation?
        if (llama_vocab_is_eog(s.vocab, new_token_id)) {
            break;
        }

        // convert the token to a string, and add it to the response
        char buf[256];
        int n = llama_token_to_piece(s.vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            GGML_ABORT("failed to convert token to piece\n");
        }
        response.append(buf, static_cast<size_t>(n));

        // prepare the next batch with the sampled token
        batch = llama_batch_get_one(&new_token_id, 1);
    }

    return response;
}
