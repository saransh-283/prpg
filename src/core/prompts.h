#pragma once

#include <string>
#include <vector>

namespace Prompts {

// Build a prompt that asks the LLM to generate unique NPC names.
// |count|      – how many names to generate.
// |recentNames| – names already used (to avoid duplicates).
inline std::string NpcNameGeneration(int count, const std::vector<std::string>& recentNames) {
    std::string prompt =
        "Generate " + std::to_string(count) +
        " unique fantasy villager name(s) for an RPG game. "
        "Each name should be a single first name only (no last names, no titles). "
        "Names should sound medieval/fantasy and be 3-10 characters long. "
        "Return ONLY the names, one per line, no numbering, no extra text.";

    if (!recentNames.empty()) {
        prompt += "\n\nDo NOT reuse any of these recent names: ";
        for (size_t i = 0; i < recentNames.size(); ++i) {
            if (i > 0) prompt += ", ";
            prompt += recentNames[i];
        }
        prompt += ".";
    }

    return prompt;
}

} // namespace Prompts
