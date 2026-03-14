#include "loadjson.h"

#include <fstream>
#include <iostream>

json LoadJsonFile(const char* path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "LoadJsonFile: failed to open '" << path << "'\n";
        return json();
    }
    try {
        json j;
        in >> j;
        return j;
    } catch (const std::exception& e) {
        std::cerr << "LoadJsonFile: parse error for '" << path << "': " << e.what() << "\n";
        return json();
    }
}
