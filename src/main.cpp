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
    try {
        V8Handler v8_handler;
        v8::Isolate* isolate = v8_handler.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8_handler.GetContext();
        v8::Context::Scope context_scope(context);

        // Create a JavaScript object
        v8::Local<v8::Value> obj = v8_handler.CreateJSObject("({ x: 10, y: 20 })");
        if (obj->IsNullOrUndefined()) {
            std::cerr << "Failed to create JavaScript object" << std::endl;
            return 1;
        }

        // Verify the object was created correctly
        if (obj->IsObject()) {
            v8::Local<v8::Object> obj_local = obj.As<v8::Object>();
            v8::Local<v8::Value> x_val = obj_local->Get(context, v8::String::NewFromUtf8(isolate, "x").ToLocalChecked()).ToLocalChecked();
            v8::Local<v8::Value> y_val = obj_local->Get(context, v8::String::NewFromUtf8(isolate, "y").ToLocalChecked()).ToLocalChecked();

            int x = x_val->Int32Value(context).ToChecked();
            int y = y_val->Int32Value(context).ToChecked();

            std::cout << "Initial object: x = " << x << ", y = " << y << std::endl;
        } else {
            std::cerr << "Created value is not an object" << std::endl;
            return 1;
        }

        // Define a function that modifies the object
        if (!v8_handler.ExecuteJS("function modifyObject(obj) { console.log('Object received:', obj); obj.x *= 2; obj.y += 5; console.log('Object after modification:', obj); return obj; }")) {
            std::cerr << "Failed to define modifyObject function" << std::endl;
            return 1;
        }

        // Call the function with our object
        std::cout << "Calling modifyObject function..." << std::endl;
        v8::MaybeLocal<v8::Value> maybe_result = v8_handler.CallJSFunction("modifyObject", obj);

        if (!maybe_result.IsEmpty()) {
            v8::Local<v8::Value> result = maybe_result.ToLocalChecked();
            if (result->IsObject()) {
                v8::Local<v8::Object> modified_obj = result.As<v8::Object>();

                // Read the modified values back in C++
                v8::Local<v8::Value> x_val = modified_obj->Get(context, v8::String::NewFromUtf8(isolate, "x").ToLocalChecked()).ToLocalChecked();
                v8::Local<v8::Value> y_val = modified_obj->Get(context, v8::String::NewFromUtf8(isolate, "y").ToLocalChecked()).ToLocalChecked();

                int x = x_val->Int32Value(context).ToChecked();
                int y = y_val->Int32Value(context).ToChecked();

                std::cout << "Modified object: x = " << x << ", y = " << y << std::endl;
            } else {
                std::cerr << "Result is not an object" << std::endl;
            }
        } else {
            std::cerr << "Failed to call modifyObject function" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Caught unknown exception" << std::endl;
    }

    return 0;
}