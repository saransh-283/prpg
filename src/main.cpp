#include <llama.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>

int main() {
    int ngl = 99;
    int n_ctx = 2048;
    std::string model_path = std::string(CMAKE_SOURCE_DIR) + "/src/models/Llama-SmolTalk-3.2-1B-Instruct.Q8_0.gguf";

    // load dynamic backends
    ggml_backend_load_all();
    
    // --- load model ---
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl;

    llama_model* model = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!model) {
        std::cerr << "Failed to load model: " << model_path << "\n";
        return 1;
    }
    
    const llama_vocab* vocab = llama_model_get_vocab(model);

    // initialize the context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;
    ctx_params.n_batch = n_ctx;

    llama_context* ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) {
        std::cerr << "Failed to create context\n";
        llama_model_free(model);
        return 1;
    }

    // initialize the sampler
    llama_sampler * sampler_basic = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler_basic, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(sampler_basic, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(sampler_basic, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    // --- sampler: greedy ---
    auto chain_params = llama_sampler_chain_default_params();
    llama_sampler* sampler_greedy = llama_sampler_chain_init(chain_params);
    llama_sampler_chain_add(sampler_greedy, llama_sampler_init_greedy());

    // helper function to evaluate a prompt and generate a response
    auto generate = [&](const std::string & prompt) {
        std::string response;

        const bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) == -1;

        // tokenize the prompt
        const int n_prompt_tokens = -llama_tokenize(vocab, prompt.c_str(), prompt.size(), NULL, 0, is_first, true);
        std::vector<llama_token> prompt_tokens(n_prompt_tokens);
        if (llama_tokenize(vocab, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(), is_first, true) < 0) {
            GGML_ABORT("failed to tokenize the prompt\n");
        }

        // prepare a batch for the prompt
        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
        llama_token new_token_id;
        while (true) {
            // check if we have enough space in the context to evaluate this batch
            int n_ctx = llama_n_ctx(ctx);
            int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) + 1;
            if (n_ctx_used + batch.n_tokens > n_ctx) {
                printf("\033[0m\n");
                fprintf(stderr, "context size exceeded\n");
                exit(0);
            }

            int ret = llama_decode(ctx, batch);
            if (ret != 0) {
                GGML_ABORT("failed to decode, ret = %d\n", ret);
            }

            // sample the next token
            new_token_id = llama_sampler_sample(sampler_basic, ctx, -1);

            // is it an end of generation?
            if (llama_vocab_is_eog(vocab, new_token_id)) {
                break;
            }

            // convert the token to a string, print it and add it to the response
            char buf[256];
            int n = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
            if (n < 0) {
                GGML_ABORT("failed to convert token to piece\n");
            }
            std::string piece(buf, n);
            printf("%s", piece.c_str());
            fflush(stdout);
            response += piece;

            // prepare the next batch with the sampled token
            batch = llama_batch_get_one(&new_token_id, 1);
        }

        return response;
    };

    auto format_chat = [&](const std::string& user_prompt) {
    std::string templated =
        "<|start_header_id|>system<|end_header_id|>\n\n"
        "Cutting Knowledge Date: December 2023\n"
        "Today Date: 23 July 2024\n\n"
        "You are a helpful assistant<|eot_id|><|start_header_id|>user<|end_header_id|>\n\n"
        + user_prompt + "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n";
    return templated;
};

    // --- read prompt ---
    std::cout << "Enter prompt: ";
    std::string prompt;
    std::getline(std::cin, prompt);

    // --- generate ---
    std::cout << "Response: ";
    generate(format_chat(prompt));
    std::cout << "\n";

    // --- cleanup ---
    llama_sampler_free(sampler_basic);
    llama_sampler_free(sampler_greedy);
    llama_free(ctx);
    llama_model_free(model);
    return 0;
}
