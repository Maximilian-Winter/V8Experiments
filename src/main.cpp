// src/main.cpp
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#include "v8_handler.h"
#include <iostream>
#include <fstream>
#include <sstream>

std::string ReadFile(const std::string &filename)
{
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main()
{
    try
    {
        V8Handler v8_handler;
        v8::Isolate *isolate = v8_handler.GetIsolate();
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = v8_handler.GetContext();
        v8::Context::Scope context_scope(context);

        // Register a C++ callback
        v8_handler.RegisterCallback("cppFunction", [](const v8::FunctionCallbackInfo<v8::Value> &args)
        {
            v8::Isolate *isolate = args.GetIsolate();
            std::cout << "C++ function called from JavaScript!" << std::endl;
            args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, "Hello from C++!").ToLocalChecked());
        });
        // Create a JavaScript object
        auto js_object = v8_handler.CreateJSObject("({ x: 10, y: 20 })");
        if (!js_object)
        {
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

        std::cout <<
                "Define modifyObject function: 'function modifyObject(obj) { console.log('Object received:', obj.x , obj.y); obj.x *= 2; obj.y += 5; console.log('Object after modification:', obj.x , obj.y); return obj; }'"
                << std::endl;

        // Define function in Javascript
        if (!v8_handler.ExecuteJS(
            "function modifyObject(obj) { console.log('Object received:', obj.x , obj.y); obj.x *= 2; obj.y += 5; console.log('Object after modification:', obj.x , obj.y); return obj; }"))
        {
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
        v8_handler.RegisterCallback("asyncOperation", [&v8_handler](const v8::FunctionCallbackInfo<v8::Value> &args)
        {
            v8::Isolate *isolate = args.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            // Get the resolver for the promise
            v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
            v8::Local<v8::Promise> promise = resolver->GetPromise();
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Simulate work
            resolver->Resolve(context, v8::String::NewFromUtf8(isolate, "Async operation completed!").ToLocalChecked()).
                    Check();
            args.GetReturnValue().Set(promise);
        });
        std::vector<std::future<bool> > futures;
        futures.reserve(1000);
        for (int i = 0; i < 1000; i++)
        {
            // Execute JavaScript that uses the asynchronous C++ callback
            futures.emplace_back(v8_handler.ExecuteJSAsync(R"(
            async function runAsyncOperations() {
                //print('Starting async operations');
                let result1 = await asyncOperation();
                //print('Result 1:', result1);
                let result2 = await asyncOperation();
                //print('Result 2:', result2);
                //print('All async operations completed');
            }

            runAsyncOperations();
        )"));
        }
        for (auto& future: futures)
        {
            future.get();
        }
        std::cout << "JavaScript execution completed" << std::endl;
    } catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    } catch (...)
    {
        std::cerr << "Caught unknown exception" << std::endl;
    }


    return 0;
}
