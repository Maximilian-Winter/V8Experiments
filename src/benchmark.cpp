//
// Created by maxim on 15.09.2024.
//
#include "V8EngineManager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <iomanip>

struct BenchmarkResults {
    double objectCreationTime;
    double objectModificationTime;
    double functionExecutionTime;
    double jsonSerializationTime;
    int totalIterations;
};

std::atomic<int> globalIterationCount(0);

BenchmarkResults runBenchmark(V8EngineManager& manager, int iterations, bool verbose = false) {
    BenchmarkResults results = {0, 0, 0, 0, iterations};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 1000);

    auto start = std::chrono::high_resolution_clock::now();
    auto end = start;

    for (int i = 0; i < iterations; ++i) {
        if (verbose) {
            std::cout << "Thread " << std::this_thread::get_id() << " - Iteration " << i << std::endl;
        }
        // Step 1: Create a complex JavaScript object
        start = std::chrono::high_resolution_clock::now();
        auto engine = manager.getEngine();


        auto jsObject = engine.get()->CreateJSValue(R"(
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
        end = std::chrono::high_resolution_clock::now();
        results.objectCreationTime += std::chrono::duration<double, std::milli>(end - start).count();

        // Step 2: Modify the object using JavaScript
        start = std::chrono::high_resolution_clock::now();
        engine.get()->ExecuteJS(R"(
            function modifyObject(obj, newValue) {
                obj.data.numbers.push(newValue);
                obj.data.strings.unshift('modified' + newValue);
                obj.data.nested.d = {x: newValue, y: newValue * 2};
                obj.newField = 'added in JS ' + newValue;
                return obj;
            }
        )");
        end = std::chrono::high_resolution_clock::now();
        results.objectModificationTime += std::chrono::duration<double, std::milli>(end - start).count();

        // Step 3: Execute JavaScript function
        start = std::chrono::high_resolution_clock::now();
        std::vector<std::shared_ptr<JSValueWrapper>> args;
        args.emplace_back(jsObject);
        args.emplace_back(engine.get()->CreateJSValue(std::to_string(distrib(gen))));
        auto result = engine.get()->CallJSFunction("modifyObject", args);
        end = std::chrono::high_resolution_clock::now();
        results.functionExecutionTime += std::chrono::duration<double, std::milli>(end - start).count();

        // Step 4: Serialize to JSON
        start = std::chrono::high_resolution_clock::now();
        nlohmann::json json = result->ToJson();
        end = std::chrono::high_resolution_clock::now();
        results.jsonSerializationTime += std::chrono::duration<double, std::milli>(end - start).count();

        ++globalIterationCount;
    }

    return results;
}

void printResults(const std::vector<BenchmarkResults>& allResults) {
    BenchmarkResults totalResults = {0, 0, 0, 0, 0};
    for (const auto& result : allResults) {
        totalResults.objectCreationTime += result.objectCreationTime;
        totalResults.objectModificationTime += result.objectModificationTime;
        totalResults.functionExecutionTime += result.functionExecutionTime;
        totalResults.jsonSerializationTime += result.jsonSerializationTime;
        totalResults.totalIterations += result.totalIterations;
    }

    int threadCount = allResults.size();
    double totalTime = totalResults.objectCreationTime + totalResults.objectModificationTime +
                       totalResults.functionExecutionTime + totalResults.jsonSerializationTime;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Benchmark Results:" << std::endl;
    std::cout << "Thread Count: " << threadCount << std::endl;
    std::cout << "Total Iterations: " << totalResults.totalIterations << std::endl;
    std::cout << "Total Time: " << totalTime << " ms" << std::endl;
    std::cout << "Average time per iteration: " << totalTime / totalResults.totalIterations << " ms" << std::endl;
    std::cout << "\nBreakdown:" << std::endl;
    std::cout << "Object Creation: " << totalResults.objectCreationTime << " ms ("
              << (totalResults.objectCreationTime / totalTime * 100) << "%)" << std::endl;
    std::cout << "Object Modification: " << totalResults.objectModificationTime << " ms ("
              << (totalResults.objectModificationTime / totalTime * 100) << "%)" << std::endl;
    std::cout << "Function Execution: " << totalResults.functionExecutionTime << " ms ("
              << (totalResults.functionExecutionTime / totalTime * 100) << "%)" << std::endl;
    std::cout << "JSON Serialization: " << totalResults.jsonSerializationTime << " ms ("
              << (totalResults.jsonSerializationTime / totalTime * 100) << "%)" << std::endl;
}

int main() {
    const int iterationsPerThread = 250;
    const int threadCount = 4;
    V8EngineManager manager(threadCount);
    std::vector<std::thread> threads;
    std::vector<BenchmarkResults> results(threadCount);

    auto overallStart = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&manager, &results, i, iterationsPerThread]() {
            results[i] = runBenchmark(manager, iterationsPerThread, false);
        });
    }

    // Progress reporting thread
    std::thread progressThread([iterationsPerThread, threadCount]() {
        int totalIterations = iterationsPerThread * threadCount;
        while (globalIterationCount < totalIterations) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            double progress = static_cast<double>(globalIterationCount) / totalIterations * 100;
            std::cout << "\rProgress: " << std::fixed << std::setprecision(1) << progress << "%" << std::flush;
        }
        std::cout << "\rProgress: 100.0%" << std::endl;
    });

    for (auto& thread : threads) {
        thread.join();
    }

    auto overallEnd = std::chrono::high_resolution_clock::now();
    auto overallDuration = std::chrono::duration_cast<std::chrono::milliseconds>(overallEnd - overallStart);

    progressThread.join();

    std::cout << "\nAll threads completed." << std::endl;
    std::cout << "Total execution time: " << overallDuration.count() << " ms" << std::endl;

    printResults(results);

    return 0;
}