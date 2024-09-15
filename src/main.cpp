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
            auto result = engine->ExecuteJS("console.log('Hello Fucker!');");
            if (!result) {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }

            // Register a C++ callback
            engine->RegisterCallback("cppFunction", [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* isolate = args.GetIsolate();
                std::cout << "C++ function called from JavaScript!" << std::endl;
                args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, "Hello from C++!").ToLocalChecked());
            });

            // Execute JavaScript that uses the C++ callback
            result = engine->ExecuteJS(R"(
                console.log('Calling C++ function from JavaScript');
                let result = cppFunction();
                console.log('Result:', result);
            )");

            if (!result) {
                std::cerr << "Failed to execute JavaScript" << std::endl;
                return 1;
            }

            // Create a JavaScript object
            auto js_object = engine->CreateJSValue("{ x: 10, y: 20 }");
            if (!js_object) {
                std::cerr << "Failed to create JavaScript object" << std::endl;
                return 1;
            }

            // Use the wrapper to interact with the object
            if (js_object->GetType() == JSValueWrapper::Type::Object) {
                std::cout << "Initial object: x = " << js_object->Get<int>(engine.get(), "x")
                          << ", y = " << js_object->Get<int>(engine.get(), "y") << std::endl;

                std::cout << "Modify object in C++ using the wrapper" << std::endl;

                // Modify the object using the wrapper
                js_object->Set(engine.get(), "x", 20);
                js_object->Set(engine.get(), "y", 30);

                std::cout << "Modified object: x = " << js_object->Get<int>(engine.get(), "x")
                          << ", y = " << js_object->Get<int>(engine.get(), "y") << std::endl;
            }

            // Define function in JavaScript
            result = engine->ExecuteJS(R"(
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
            std::vector<v8::Local<v8::Value>> args;
            args.emplace_back(js_object->GetV8Value(engine.get()));

            auto modified_object = engine->CallJSFunction("modifyObject", args);
            if (modified_object && modified_object->GetType() == JSValueWrapper::Type::Object) {
                std::cout << "Object after calling modifyObject: x = " << modified_object->Get<int>(engine.get(), "x")
                          << ", y = " << modified_object->Get<int>(engine.get(), "y") << std::endl;
            }

            engine->RegisterCallback("asyncOperation", [](const v8::FunctionCallbackInfo<v8::Value> &args)
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
            auto async_future = engine->ExecuteJSAsync(R"(
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
            auto various_types = engine->ExecuteJS(R"(
                ({
                    number_data: 42,
                    string_data: "Hello, World!",
                    boolean_data: true,
                    array_data: [1, 2, 3],
                    nested_data: { a: 1, b: 2 }
                })
            )");

            if (various_types && various_types->GetType() == JSValueWrapper::Type::Object) {
                std::cout << "Number: " << various_types->Get<int>(engine.get(), "number_data") << std::endl;
                std::cout << "String: " << various_types->Get<std::string>(engine.get(), "string_data") << std::endl;
                std::cout << "Boolean: " << (various_types->Get<bool>(engine.get(), "boolean_data") ? "true" : "false") << std::endl;

                auto array = various_types->ToJson(engine.get());
                std::cout << "Array: [";
                for (uint32_t i = 0; i < array.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << array;
                }
                std::cout << "]" << std::endl;

                /*auto nested = various_types->Get<v8::Local<v8::Object>>(engine.get(), "nested_data");
                v8::Local<v8::Value> a_value, b_value;
                if (nested->Get(engine->GetLocalContext(), v8::String::NewFromUtf8(engine->GetIsolate(), "a").ToLocalChecked()).ToLocal(&a_value) &&
                    nested->Get(engine->GetLocalContext(), v8::String::NewFromUtf8(engine->GetIsolate(), "b").ToLocalChecked()).ToLocal(&b_value)) {
                    std::cout << "Nested: { a: " << a_value->Int32Value(engine->GetLocalContext()).FromMaybe(0)
                              << ", b: " << b_value->Int32Value(engine->GetLocalContext()).FromMaybe(0) << " }" << std::endl;
                }*/
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