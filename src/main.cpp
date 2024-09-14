// src/main.cpp
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
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



        // Register a C++ callback
        v8_handler.RegisterCallback("cppFunction", [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* isolate = args.GetIsolate();
            std::cout << "C++ function called from JavaScript!" << std::endl;
            args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, "Hello from C++!").ToLocalChecked());
        });
        // Execute JavaScript that uses the C++ callback
        v8_handler.ExecuteJS(R"(
            console.log('Calling C++ function from JavaScript');
            let result = cppFunction();
            console.log('Result:', result);
        )");

        // Register a C++ callback that simulates an asynchronous operation
        v8_handler.RegisterCallback("asyncOperation", [&v8_handler](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* isolate = args.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            // Get the resolver for the promise
            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            v8::Local<v8::Promise> promise = resolver->GetPromise();
            std::this_thread::sleep_for(std::chrono::seconds(1));  // Simulate work
            resolver->Resolve(context, v8::String::NewFromUtf8(isolate, "Async operation completed!").ToLocalChecked()).Check();
            args.GetReturnValue().Set(promise);
        });

        // Execute JavaScript that uses the asynchronous C++ callback
        auto future2 = v8_handler.ExecuteJSAsync(R"(
            async function runAsyncOperations() {
                console.log('Starting async operations');
                let result1 = await asyncOperation();
                console.log('Result 1:', result1);
                let result2 = await asyncOperation();
                console.log('Result 2:', result2);
                console.log('All async operations completed');
            }

            runAsyncOperations();
        )");


        if (future2.get()) {
            std::cout << "Asynchronous JavaScript execution completed successfully" << std::endl;
        } else {
            std::cout << "Asynchronous JavaScript execution failed" << std::endl;
        }
        std::cout << "JavaScript execution completed" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Caught unknown exception" << std::endl;
    }


    return 0;
}