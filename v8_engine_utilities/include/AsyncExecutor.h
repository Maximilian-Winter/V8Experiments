//
// Created by maxim on 16.09.2024.
//
#pragma once
#include <functional>
class AsyncExecutor
{
public:
    using TaskFunction = std::function<void()>;
    virtual ~AsyncExecutor() = default;
    virtual void ExecuteAsync(const TaskFunction &task_function) = 0;
};
