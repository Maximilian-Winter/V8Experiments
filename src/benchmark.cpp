//
// Created by maxim on 15.09.2024.
//
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0
#include "V8EngineManager.h"
#include <iostream>
#include <chrono>

void runBenchmark(V8EngineManager& manager, int iterations) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        std::cout << i << " iteration" << std::endl;
        auto engine = manager.getEngine();
        // Step 1: Create a complex JavaScript object
        auto jsObject = engine->CreateJSValue(R"(
            {
                name: 'Complex Object',
                data: {
                    numbers: [1, 2, 3, 4, 5],
                    strings: ['hello', 'world'],
                    nested: {
                        a: 1,
                        b: 'two',
                        c: [true, false, null]
                    }
                },
                timestamp: new Date().getTime()
            }
        )");

        // Step 2: Modify the object using JavaScript
        engine->ExecuteJS(R"(
            function modifyObject(obj) {
                obj.data.numbers.push(6);
                obj.data.strings.unshift('modified');
                obj.data.nested.d = {x: 10, y: 20};
                obj.newField = 'added in JS';
                return obj;
            }
        )");

        std::vector<v8::Local<v8::Value>> args;
        args.emplace_back(jsObject->GetV8Value(engine.get()));
        engine->CallJSFunction("modifyObject", args);
        nlohmann::json json = jsObject->ToJson(engine.get());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Benchmark completed " << iterations << " iterations in "
              << duration.count() << " ms" << std::endl;
    std::cout << "Average time per iteration: "
              << static_cast<double>(duration.count()) / iterations << " ms" << std::endl;
}

int main() {
    const int iterations = 1000;
    V8EngineManager manager(4);  // Create a manager with 4 engine instances
    std::vector<std::thread> threads;

    threads.reserve(4);
    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back([&manager]()
        {
            runBenchmark(manager, iterations);
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }
    return 0;
}