#include "llm.h"
#include "../../core/config.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>

static llama_model* g_model = nullptr;
static llama_context* g_ctx = nullptr;
static const llama_vocab* g_vocab = nullptr;
static llama_sampler* g_sampler_basic = nullptr;

static std::mutex g_llm_mutex;
static std::thread g_loader_thread;
static std::atomic<bool> g_loading{false};
static std::atomic<bool> g_ready{false};
static std::string g_last_error;

static void set_last_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_llm_mutex);
    g_last_error = msg;
}

static void clear_last_error() {
    std::lock_guard<std::mutex> lock(g_llm_mutex);
    g_last_error.clear();
}

static void free_state_locked() {
    if (g_sampler_basic) {
        llama_sampler_free(g_sampler_basic);
        g_sampler_basic = nullptr;
    }
    if (g_ctx) {
        llama_free(g_ctx);
        g_ctx = nullptr;
    }
    if (g_model) {
        llama_model_free(g_model);
        g_model = nullptr;
    }
    g_vocab = nullptr;
    g_ready = false;
}

static bool load_state(const std::string& model_path, int ngl, int n_ctx,
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

    // load dynamic backends
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
    ctx_params.n_batch = n_ctx;

    out_ctx = llama_init_from_model(out_model, ctx_params);
    if (!out_ctx) {
        llama_model_free(out_model);
        out_model = nullptr;
        out_error = "Failed to create context";
        return false;
    }

    // initialize the sampler
    out_sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(out_sampler, llama_sampler_init_min_p(Config::LLM::MIN_P, 1));
    llama_sampler_chain_add(out_sampler, llama_sampler_init_temp(Config::LLM::TEMPERATURE));
    llama_sampler_chain_add(out_sampler, llama_sampler_init_dist(Config::LLM::DEFAULT_SEED));

    return true;
}

static std::string format_chat(const std::string& user_prompt) {
    std::string templated =
        "<|start_header_id|>system<|end_header_id|>\n\n"
        "Cutting Knowledge Date: December 2023\n"
        "Today Date: 23 July 2024\n\n"
        "You are a helpful assistant<|eot_id|><|start_header_id|>user<|end_header_id|>\n\n"
        + user_prompt + "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n";
    return templated;
}

bool initialize_llm(const std::string& model_path, int ngl, int n_ctx) {
    wait_for_llm_load();
    clear_last_error();

    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    llama_sampler* sampler = nullptr;
    std::string err;
    const bool ok = load_state(model_path, ngl, n_ctx, model, ctx, vocab, sampler, err);
    if (!ok) {
        set_last_error(err);
        std::cerr << err << "\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_llm_mutex);
        free_state_locked();
        g_model = model;
        g_ctx = ctx;
        g_vocab = vocab;
        g_sampler_basic = sampler;
        g_ready = true;
    }

    return true;
}

bool start_llm_background_load(const std::string& model_path, int ngl, int n_ctx) {
    if (g_ready.load()) return false;
    bool expected = false;
    if (!g_loading.compare_exchange_strong(expected, true)) {
        // already loading
        return false;
    }

    clear_last_error();

    // If a previous loader thread finished but wasn't joined yet, join it now.
    if (g_loader_thread.joinable()) {
        g_loader_thread.join();
    }

    g_loader_thread = std::thread([model_path, ngl, n_ctx]() {
        llama_model* model = nullptr;
        llama_context* ctx = nullptr;
        const llama_vocab* vocab = nullptr;
        llama_sampler* sampler = nullptr;
        std::string err;

        const bool ok = load_state(model_path, ngl, n_ctx, model, ctx, vocab, sampler, err);
        if (!ok) {
            set_last_error(err);
            std::cerr << "[LLM] " << err << "\n";
            g_loading = false;
            g_ready = false;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_llm_mutex);
            free_state_locked();
            g_model = model;
            g_ctx = ctx;
            g_vocab = vocab;
            g_sampler_basic = sampler;
            g_ready = true;
        }

        g_loading = false;
        std::cout << "[LLM] Model loaded in background." << std::endl;
    });

    return true;
}

bool is_llm_loading() {
    return g_loading.load();
}

bool is_llm_ready() {
    return g_ready.load();
}

std::string llm_last_error() {
    std::lock_guard<std::mutex> lock(g_llm_mutex);
    return g_last_error;
}

void wait_for_llm_load() {
    if (g_loader_thread.joinable()) {
        g_loader_thread.join();
    }
    // If the thread finished, g_loading may already be false; enforce it.
    g_loading = false;
}

std::string generate_from_prompt(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(g_llm_mutex);
    if (!g_ctx || !g_model || !g_vocab || !g_sampler_basic) return "";

    // Don't modify the caller's argument (it's a const ref). Create a formatted prompt
    std::string formatted = format_chat(prompt);

    std::string response;

    const bool is_first = llama_memory_seq_pos_max(llama_get_memory(g_ctx), 0) == -1;

    // tokenize the prompt
    const int n_prompt_tokens = -llama_tokenize(g_vocab, formatted.c_str(), formatted.size(), NULL, 0, is_first, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    if (llama_tokenize(g_vocab, formatted.c_str(), formatted.size(), prompt_tokens.data(), prompt_tokens.size(), is_first, true) < 0) {
        GGML_ABORT("failed to tokenize the prompt\n");
    }

    // prepare a batch for the prompt
    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    llama_token new_token_id;
    while (true) {
        // check if we have enough space in the context to evaluate this batch
        int n_ctx = llama_n_ctx(g_ctx);
        int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(g_ctx), 0) + 1;
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            fprintf(stderr, "context size exceeded\n");
            break;
        }

        int ret = llama_decode(g_ctx, batch);
        if (ret != 0) {
            GGML_ABORT("failed to decode, ret = %d\n", ret);
        }

        // sample the next token
        new_token_id = llama_sampler_sample(g_sampler_basic, g_ctx, -1);

        // is it an end of generation?
        if (llama_vocab_is_eog(g_vocab, new_token_id)) {
            break;
        }

        // convert the token to a string, and add it to the response
        char buf[256];
        int n = llama_token_to_piece(g_vocab, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            GGML_ABORT("failed to convert token to piece\n");
        }
        std::string piece(buf, n);
        response += piece;

        // prepare the next batch with the sampled token
        batch = llama_batch_get_one(&new_token_id, 1);
    }

    return response;
}

void shutdown_llm() {
    wait_for_llm_load();

    std::lock_guard<std::mutex> lock(g_llm_mutex);
    free_state_locked();
}
