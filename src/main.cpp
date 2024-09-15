// src/main.cpp
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#include "V8EngineManager.h"
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

        V8EngineManager v8Manager;
        {
            V8EngineManager::V8EngineGuard engine = v8Manager.getEngine();
            if(!engine->ExecuteJS("console.log('Hello Fucker!');"))
            {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }

            // Register a C++ callback
            engine->RegisterCallback("cppFunction", [](const v8::FunctionCallbackInfo<v8::Value> &args)
            {
                v8::Isolate *isolate = args.GetIsolate();
                std::cout << "C++ function called from JavaScript!" << std::endl;
                args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, "Hello from C++!").ToLocalChecked());
            });
            // Execute JavaScript that uses the C++ callback
            bool result = engine->ExecuteJS(R"(
                console.log('Calling C++ function from JavaScript');
                let result = cppFunction();
                console.log('Result:', result);
            )");

            if(!result)
            {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }

            auto js_object = engine->CreateJSObject("{ x: 10, y: 20 }");
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
                    "Define modifyObject function"
                    << std::endl;

            // Define function in Javascript
            if (!engine->ExecuteJS(
                "function modifyObject(obj) { console.log('Object received:', obj.x , obj.y); obj.x *= 2; obj.y += 5; console.log('Object after modification:', obj.x , obj.y); return obj; }"))
            {
                std::cerr << "Failed to define modifyObject function" << std::endl;
                return 1;
            }

            std::cout << "Calling modifyObject function in Javascript" << std::endl;

            std::vector<v8::Local<v8::Value>> args;
            args.emplace_back(js_object->GetV8Object());

            engine->CallJSFunction("modifyObject", args);

            std::cout << "Object after calling modifyObject: x = " << js_object->Get<int>("x")
                           << ", y = " << js_object->Get<int>("y") << std::endl;
            // Register a C++ callback that simulates an asynchronous operation
            engine->RegisterCallback("asyncOperation", [](const v8::FunctionCallbackInfo<v8::Value> &args)
            {
                v8::Isolate *isolate = args.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                // Get the resolver for the promise
                v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
                v8::Local<v8::Promise> promise = resolver->GetPromise();
                std::this_thread::sleep_for(std::chrono::seconds(4)); // Simulate work
                resolver->Resolve(context, v8::String::NewFromUtf8(isolate, "Async operation completed!").ToLocalChecked()).
                        Check();
                args.GetReturnValue().Set(promise);
            });

            result = engine->ExecuteJS(R"(
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
            if(!result)
            {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }
            std::cout << "JavaScript execution completed" << std::endl;
        }

    } catch (const std::exception &e)
    {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    } catch (...)
    {
        std::cerr << "Caught unknown exception" << std::endl;
    }


    return 0;
}
