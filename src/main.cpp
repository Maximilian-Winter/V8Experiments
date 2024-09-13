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

        // Register a C++ callback
        v8_handler.RegisterCallback("cppFunction", [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* isolate = args.GetIsolate();
            v8::HandleScope handle_scope(isolate);
            std::cout << "C++ function called from JavaScript!" << std::endl;
            args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, "Hello from C++!").ToLocalChecked());
        });
        // Create a JavaScript object
        auto js_object = v8_handler.CreateJSObject("({ x: 10, y: 20 })");
        if (!js_object) {
            std::cerr << "Failed to create JavaScript object" << std::endl;
            return 1;
        }

        // Use the wrapper to interact with the object
        std::cout << "Initial object: x = " << js_object->Get<int>("x")
                  << ", y = " << js_object->Get<int>("y") << std::endl;

        std::cout << "Modify object in C++ using the wrapper" << std::endl;

        // Modify the object using the wrapper
        js_object->Set("x", 20);
        js_object->Set("y", 30);

        std::cout << "Modified object: x = " << js_object->Get<int>("x")
                  << ", y = " << js_object->Get<int>("y") << std::endl;

        std::cout << "Define modifyObject function: 'function modifyObject(obj) { console.log('Object received:', obj.x , obj.y); obj.x *= 2; obj.y += 5; console.log('Object after modification:', obj.x , obj.y); return obj; }'" << std::endl;

        // Define function in Javascript
        if (!v8_handler.ExecuteJS("function modifyObject(obj) { console.log('Object received:', obj.x , obj.y); obj.x *= 2; obj.y += 5; console.log('Object after modification:', obj.x , obj.y); return obj; }")) {
            std::cerr << "Failed to define modifyObject function" << std::endl;
            return 1;
        }

        // Call the function with our object
        std::cout << "Calling modifyObject function in Javascript" << std::endl;

        v8_handler.CallJSFunction("modifyObject", js_object->GetV8Object());

        std::cout << "Modified object: x = " << js_object->Get<int>("x")
                                 << ", y = " << js_object->Get<int>("y") << std::endl;
        // Execute JavaScript that uses the C++ callback
        v8_handler.ExecuteJS(R"(
            console.log('Calling C++ function from JavaScript');
            let result = cppFunction();
            console.log('Result:', result);
        )");

        // Register a C++ callback that simulates an asynchronous operation
        v8_handler.RegisterCallback("asyncOperation", [&v8_handler](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Isolate* isolate = args.GetIsolate();
            v8::HandleScope handle_scope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            // Get the resolver for the promise
            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            v8::Local<v8::Promise> promise = resolver->GetPromise();

            // Simulate an asynchronous operation
            v8_handler.EnqueueAsyncTask([isolate, resolver]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));  // Simulate work

                v8::Isolate::Scope isolate_scope(isolate);
                v8::HandleScope handle_scope(isolate);
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Context::Scope context_scope(context);

                resolver->Resolve(context, v8::String::NewFromUtf8(isolate, "Async operation completed!").ToLocalChecked()).Check();
            });

            args.GetReturnValue().Set(promise);
        });

        // Execute JavaScript that uses the asynchronous C++ callback
        std::cout << "Starting asynchronous JavaScript execution..." << std::endl;
        auto future = v8_handler.ExecuteJSAsync(R"(
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

        std::cout << "Asynchronous JavaScript execution started. Waiting for completion..." << std::endl;

        // Wait for the JavaScript execution to complete
        future.wait();

        if (future.get()) {
            std::cout << "Asynchronous JavaScript execution completed successfully" << std::endl;
        } else {
            std::cout << "Asynchronous JavaScript execution failed" << std::endl;
        }

        // Allow some time for any remaining async operations to complete
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::cout << "JavaScript execution completed" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Caught unknown exception" << std::endl;
    }


    return 0;
}