// src/main.cpp
#include "v8_handler.h"
#include <iostream>
#include <fstream>
#include <sstream>

std::string ReadFile(const std::string& filename) {
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main() {
    V8Handler v8_handler;
    std::string js_code = ReadFile("test.js");
    std::string result = v8_handler.ExecuteJS(js_code);
    std::cout << "Result: " << result << std::endl;
    return 0;
}
