#include <iostream>
#include "perlin_roads.h"
#include "llm.h"
#include <filesystem>

int main()
{
    std::cout << "Hello, world!" << std::endl;

    // Example: generate a roads canvas and keep it in memory for later use.
    // We don't write it to disk here. The canvas is a single-channel grayscale
    // image stored as a vector<uint8_t> with size = canvas_size * canvas_size.
    int canvas_size = 512;
    auto canvas = generate_perlin_roads(canvas_size, 50, 400, 1.0f, 0.01f, 42, 4, 1.0f, 2);

    // canvas is available for later processing (saving, analysis, mesh generation, etc.)

    // Try to initialize the LLM if the model exists. This is optional and
    // fails silently if the model file is missing.
    std::string model_path = std::string(CMAKE_SOURCE_DIR) + "/src/models/Llama-SmolTalk-3.2-1B-Instruct.Q8_0.gguf";
    if (std::filesystem::exists(model_path))
    {
        if (initialize_llm(model_path))
        {
            // --- read prompt ---
            std::cout << "Enter prompt: ";
            std::string prompt;
            std::getline(std::cin, prompt);
            auto resp = generate_from_prompt(prompt);
            std::cout << "LLM response: " << resp << std::endl;
            shutdown_llm();
        }
        else
        {
            std::cout << "LLM failed to initialize" << std::endl;
        }
    }
    else
    {
        std::cout << "Model not found at: " << model_path << " — skipping LLM init." << std::endl;
    }

    return 0;
}
