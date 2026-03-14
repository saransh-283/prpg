#pragma once

#include <nlohmann/json.hpp>

using nlohmann::json;

// Load a JSON file from disk. Returns a parsed json object on success,
// or `json()` (null) on failure.
json LoadJsonFile(const char* path);
