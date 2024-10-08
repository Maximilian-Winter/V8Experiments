// src/main.cpp
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#include "V8EngineManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

std::string ReadFile(const std::string &filename)
{
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


int main() {

    try {
        V8EngineManager v8Manager;
        {
            V8EngineManager::V8EngineGuard engine = v8Manager.getEngine();

            // Execute JavaScript
            auto result = engine.get()->ExecuteJS("console.log('Hello Fucker!');");
            if (!result) {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }

            // Register a C++ callback
            engine.get()->RegisterCallback("cppFunction", [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* isolate = args.GetIsolate();
                std::cout << "C++ function called from JavaScript!" << std::endl;
                args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, "Hello from C++!").ToLocalChecked());
            });

            // Execute JavaScript that uses the C++ callback
            result = engine.get()->ExecuteJS(R"(
                console.log('Calling C++ function from JavaScript');
                let result = cppFunction();
                console.log('Result:', result);
            )");

            if (!result) {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }

            // Create a JavaScript object
            auto js_object = engine.get()->CreateJSValue("{ x: 10, y: 20 }");
            if (!js_object) {
                std::cerr << "Failed to create JavaScript object" << std::endl;
                return 1;
            }

            // Use the wrapper to interact with the object
            if (js_object->GetType() == JSValueWrapper::Type::Object) {
                std::cout << "Initial object: x = " << js_object->Get<int>( "x")
                          << ", y = " << js_object->Get<int>( "y") << std::endl;

                std::cout << "Modify object in C++ using the wrapper" << std::endl;

                // Modify the object using the wrapper
                js_object->Set( "x", 20);
                js_object->Set( "y", 30);

                std::cout << "Modified object: x = " << js_object->Get<int>( "x")
                          << ", y = " << js_object->Get<int>( "y") << std::endl;
            }

            // Define function in JavaScript
            result = engine.get()->ExecuteJS(R"(
                function modifyObject(obj) {
                    console.log('Object received:', obj.x, obj.y);
                    obj.x *= 2;
                    obj.y += 5;
                    console.log('Object after modification:', obj.x, obj.y);
                    return obj;
                }
            )");

            if (!result) {
                std::cerr << "Failed to define modifyObject function" << std::endl;
                return 1;
            }

            // Call JavaScript function
            std::vector<std::shared_ptr<JSValueWrapper>> args;
            args.emplace_back(js_object);
            engine.get()->CallJSFunction("modifyObject", args);

            auto modified_object = engine.get()->CallJSFunction("modifyObject", args);
            if (modified_object && modified_object->GetType() == JSValueWrapper::Type::Object) {
                std::cout << "Object after calling modifyObject: x = " << modified_object->Get<int>( "x")
                          << ", y = " << modified_object->Get<int>( "y") << std::endl;
            }

            engine.get()->RegisterCallback("asyncOperation", [](const v8::FunctionCallbackInfo<v8::Value> &args)
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

            // Execute asynchronous operations
            auto async_future = engine.get()->ExecuteJSAsync(R"(
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
            std::this_thread::sleep_for(std::chrono::seconds(5));

            // Wait for async operations to complete
            result = async_future.get();
            if (!result) {
                std::cerr << "Failed to execute asynchronous JavaScript" << std::endl;
                return 1;
            }
            std::cout << "JavaScript execution completed" << std::endl;

            // Example of handling different types
            auto various_types = engine.get()->ExecuteJS(R"(
                ({
                    number_data: 42,
                    string_data: "Hello, World!",
                    boolean_data: true,
                    array_data: [1, 2, 3],
                    nested_data: { a: 1, b: 2 }
                })
            )");

            if (various_types && various_types->GetType() == JSValueWrapper::Type::Object) {

                auto array = various_types->ToJson();
                std::cout << "Various Types Object: {\n";
                for (const auto& [key, value] : array.items()) {
                    std::cout << "  " <<key << ": " << value.dump(4) << std::endl;
                }
                std::cout << "}" << std::endl;
            }

        }
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Caught unknown exception" << std::endl;
        return 1;
    }

    return 0;
}