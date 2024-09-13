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
    std::string output = v8_handler.ExecuteJS(ReadFile("test.js"));
    std::cout<< output << std::endl;
    // Create a JavaScript object
    v8::Local<v8::Value> obj = v8_handler.CreateJSObject("my_obj = { x: 10, y: 20 }");

    // Define a function that modifies the object
    v8_handler.DefineJSFunction("modifyObject", "function(obj) { obj.x *= 2; obj.y += 5; }");

    // Call the function with our object
    std::vector<v8::Local<v8::Value>> args = { obj };
    v8_handler.CallJSFunction("modifyObject", args);

    // Read the modified values back in C++
    v8::Local<v8::Object> modified_obj = obj.As<v8::Object>();
    v8::Local<v8::Context> context = v8_handler.GetIsolate()->GetCurrentContext();
    int x = modified_obj->Get(context, v8::String::NewFromUtf8(v8_handler.GetIsolate(), "x").ToLocalChecked()).ToLocalChecked()->Int32Value(context).ToChecked();
    int y = modified_obj->Get(context, v8::String::NewFromUtf8(v8_handler.GetIsolate(), "y").ToLocalChecked()).ToLocalChecked()->Int32Value(context).ToChecked();

    std::cout << "Modified x: " << x << ", Modified y: " << y << std::endl;  // Should print "Modified x: 20, Modified y: 25"
    return 0;
}
