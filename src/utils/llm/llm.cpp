#include "llm.h"
#include "../../core/config.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>

static llama_model* g_model = nullptr;
static llama_context* g_ctx = nullptr;
static const llama_vocab* g_vocab = nullptr;
static llama_sampler* g_sampler_basic = nullptr;

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
    // load dynamic backends
    ggml_backend_load_all();

    // --- load model ---
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;

    g_model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!g_model) {
        std::cerr << "Failed to load model: " << model_path << "\n";
        return false;
    }
    g_vocab = llama_model_get_vocab(g_model);

    // initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;
    ctx_params.n_batch = n_ctx;

    g_ctx = llama_init_from_model(g_model, ctx_params);
    if (!g_ctx) {
        std::cerr << "Failed to create context\n";
        llama_model_free(g_model);
        g_model = nullptr;
        return false;
    }

    // initialize the sampler
    g_sampler_basic = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(g_sampler_basic, llama_sampler_init_min_p(Config::LLM::MIN_P, 1));
    llama_sampler_chain_add(g_sampler_basic, llama_sampler_init_temp(Config::LLM::TEMPERATURE));
    llama_sampler_chain_add(g_sampler_basic, llama_sampler_init_dist(Config::LLM::DEFAULT_SEED));

    return true;
}

std::string generate_from_prompt(const std::string& prompt) {
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
}
