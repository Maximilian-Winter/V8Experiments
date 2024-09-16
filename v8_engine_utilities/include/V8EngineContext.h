#pragma once
#include <future>
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <utility>
#include "V8PlatformContext.h"
#include "V8JavascriptValueWrapper.h"
#include "V8CallbackHandler.h"
#include "AsyncExecutor.h"
using json = nlohmann::json;


class V8EngineContext: public AsyncExecutor, public std::enable_shared_from_this<V8EngineContext>
{
    std::function<void(const std::string &)> console_log_callback;
    std::shared_ptr<v8::Platform> platform;
    v8::Isolate *isolate{};
    std::shared_ptr<v8::Global<v8::Context> > context;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
    V8CallbackManager callback_manager_;

    std::queue<TaskFunction> task_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> should_stop{false};
    std::atomic<bool> is_stopped{false};
    std::thread execution_thread;

    void ExecutionLoop()
    {
        allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(v8::ArrayBuffer::Allocator::NewDefaultAllocator());

        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = allocator.get();

        // Create the isolate
        isolate = v8::Isolate::New(create_params);

        while (!should_stop)
        {
            TaskFunction task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { return !task_queue.empty() || should_stop; });
                if (should_stop) break;
                task = std::move(task_queue.front());
                task_queue.pop();
            }
            {
                v8::Isolate::Scope isolate_scope(isolate);
                task();
            }

        }
        is_stopped = true;
    }



public:
    explicit V8EngineContext(const V8PlatformContext &platform)
        : platform(platform.GetPlatform()), context(std::make_shared<v8::Global<v8::Context> >())
    {
        execution_thread = std::thread(&V8EngineContext::ExecutionLoop, this);
    }

    ~V8EngineContext() override
    {
        V8EngineContext::ExecuteAsync([this]()
        {
            // Dispose of persistent handles first
            context->Reset();
            // Dispose of the isolate
            isolate->Dispose();
        });
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            should_stop = true;
        }

        queue_cv.notify_one();
        if (execution_thread.joinable())
        {
            execution_thread.join();
        }
    }

    void ExecuteAsync(const TaskFunction &task_function) override
    {
        {
            TaskFunction task = task_function;
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push(std::move(task));
        }
        queue_cv.notify_one();
    }

    void Reset()
    {
        ExecuteAsync([this]()
        {

            v8::HandleScope handle_scope(isolate);
            ClearCallbacks();
            if (!context->IsEmpty()) {
                context->Reset();
            }

            // Create a new context
            v8::Local<v8::Context> local_context = v8::Context::New(isolate);
            // Create a persistent handle from the local handle
            context->Reset(isolate, local_context);
            InitializeConsole();
            return nullptr;
        });
    }

    bool IsStopped() const
    {
        return is_stopped.load(std::memory_order::memory_order_acquire);
    }

    void StopExecutionLoop()
    {
        should_stop = true;
        queue_cv.notify_one();
    }

    void SetConsoleLogCallback(std::function<void(const std::string &)> callback)
    {
        console_log_callback = std::move(callback);
    }

    void RegisterCallback(const std::string &name, V8CallbackManager::JavascriptCallback callback)
    {
        callback_manager_.RegisterCallback(name, std::move(callback));
    }

    void ClearCallbacks()
    {
        callback_manager_.ClearCallbacks();
    }

    std::shared_ptr<JSValueWrapper> ExecuteJS(const std::string &js_code)
    {
        std::future<std::shared_ptr<JSValueWrapper> > future = ExecuteJSAsync(js_code);
        return future.get();
    }

    std::future<std::shared_ptr<JSValueWrapper> > ExecuteJSAsync(const std::string &js_code)
    {
        std::shared_ptr<std::promise<std::shared_ptr<JSValueWrapper>>> promise = std::make_shared<std::promise<std::shared_ptr<JSValueWrapper>>>();
        std::future<std::shared_ptr<JSValueWrapper> > future = promise->get_future();
        ExecuteAsync([this, js_code, promise]()
        {

            v8::HandleScope handle_scope(isolate);
            const v8::Local<v8::Context> local_context = GetLocalContext();
            v8::Context::Scope context_scope(local_context);
            const v8::TryCatch try_catch(isolate);
            const v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
            v8::MaybeLocal<v8::Script> maybe_script = v8::Script::Compile(local_context, source);

            if (maybe_script.IsEmpty())
            {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "Error compiling JS code: " << *error << std::endl;
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }

            v8::Local<v8::Script> script = maybe_script.ToLocalChecked();

            callback_manager_.ExposeCallbacks(isolate, local_context);
            v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

            if (try_catch.HasCaught())
            {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "JavaScript error: " << *error << std::endl;
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }

            if (maybe_result.IsEmpty())
            {
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }

            promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, maybe_result.ToLocalChecked(), shared_from_this()));
        });

        return future;
    }

    std::shared_ptr<JSValueWrapper> CreateJSValue(const std::string &js_code)
    {
        std::future<std::shared_ptr<JSValueWrapper> > future = CreateJSValueAsync(js_code);
        return future.get();
    }

    std::future<std::shared_ptr<JSValueWrapper> > CreateJSValueAsync(const std::string &js_code)
    {
        std::shared_ptr<std::promise<std::shared_ptr<JSValueWrapper>>> promise = std::make_shared<std::promise<std::shared_ptr<JSValueWrapper>>>();
        std::future<std::shared_ptr<JSValueWrapper> > future = promise->get_future();
        ExecuteAsync([this, js_code, promise]()
        {

            v8::HandleScope handle_scope(isolate);
            const v8::Local<v8::Context> local_context = GetLocalContext();
            v8::Context::Scope context_scope(local_context);
            const std::string final_code = "(" + js_code + ")";
            const v8::TryCatch try_catch(isolate);
            const v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, final_code.c_str()).ToLocalChecked();

            v8::MaybeLocal<v8::Script> maybe_script = v8::Script::Compile(local_context, source);
            if (maybe_script.IsEmpty())
            {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "Error compiling JS code: " << *error << std::endl;
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }

            v8::Local<v8::Script> script = maybe_script.ToLocalChecked();
            v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

            if (maybe_result.IsEmpty())
            {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "Error executing JS code: " << *error << std::endl;
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }

            v8::Local<v8::Value> result = maybe_result.ToLocalChecked();
            promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, result, shared_from_this()));
        });

        return future;
    }

    std::shared_ptr<JSValueWrapper> CallJSFunction(const std::string& function_name,
                                               const std::vector<std::shared_ptr<JSValueWrapper>>& args)
    {
        std::future<std::shared_ptr<JSValueWrapper> > future = CallJSFunctionAsync(function_name, args);
        return future.get();
    }

    std::future<std::shared_ptr<JSValueWrapper> > CallJSFunctionAsync(std::string function_name,
                                                                      const std::vector<std::shared_ptr<JSValueWrapper>>& args)
    {
        std::shared_ptr<std::promise<std::shared_ptr<JSValueWrapper>>> promise = std::make_shared<std::promise<std::shared_ptr<JSValueWrapper>>>();
        std::future<std::shared_ptr<JSValueWrapper> > future = promise->get_future();
        ExecuteAsync([this, promise, function_name, &args]()
        {
            v8::HandleScope handle_scope(isolate);
            const v8::Local<v8::Context> local_context = GetLocalContext();
            v8::Context::Scope context_scope(local_context);

            const v8::TryCatch try_catch(isolate);
            const v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate, function_name.c_str()).
                    ToLocalChecked();
            v8::Local<v8::Value> func_val;
            if (!local_context->Global()->Get(local_context, func_name).ToLocal(&func_val) || !func_val->IsFunction())
            {
                std::cerr << "Function " << function_name << " not found or is not a function" << std::endl;
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }
            // Convert JSValueWrapper instances to v8::Local<v8::Value> within the execution thread
            std::vector<v8::Local<v8::Value>> local_args;
            local_args.reserve(args.size());
            for (const auto& arg : args)
            {
                local_args.push_back(arg->GetV8ValueInternal());
            }
            const v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);

            const v8::MaybeLocal<v8::Value> result = func->Call(local_context, v8::Undefined(isolate),
                                                                static_cast<int>(local_args.size()), local_args.data());

            if (try_catch.HasCaught())
            {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "Error calling function " << function_name << ": " << *error << std::endl;
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }
            v8::Local<v8::Value> result_value;
            if (!result.ToLocal(&result_value))
            {
                promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, v8::Undefined(isolate), shared_from_this()));
                return;
            }
            promise->set_value(std::make_shared<JSValueWrapper>(isolate, context, result_value, shared_from_this()));
        });
        return future;
    }

    [[nodiscard]] v8::Local<v8::Context> GetLocalContext() const
    {
        return context->Get(isolate);
    }

    [[nodiscard]] v8::Isolate *GetIsolate()
    {
        std::promise<v8::Isolate *> promise;
        std::future<v8::Isolate *> future = promise.get_future();
        ExecuteAsync([this, &promise]()
        {
            promise.set_value(isolate);
        });

        return future.get();
    }

private:
    void InitializeConsole()
    {
        v8::Local<v8::Context> local_context = context->Get(isolate);
        v8::Context::Scope context_scope(local_context);

        v8::Local<v8::Object> global = local_context->Global();

        v8::Local<v8::Object> console = v8::Object::New(isolate);
        v8::Local<v8::Function> log_function = v8::Function::New(
                    local_context,
                    [](const v8::FunctionCallbackInfo<v8::Value> &args)
                    {
                        v8::Local<v8::Value> data = args.Data();
                        auto *handler = static_cast<V8EngineContext *>(v8::External::Cast(*data)->Value());
                        v8::Isolate *isolate = args.GetIsolate();
                        v8::Local<v8::Context> context = isolate->GetCurrentContext();

                        std::string result;
                        for (int i = 0; i < args.Length(); i++)
                        {
                            v8::Local<v8::Value> arg = args[i];
                            v8::Local<v8::String> str;
                            if (!arg->ToString(context).ToLocal(&str))
                            {
                                // Skip this argument if it cannot be converted to a string
                                continue;
                            }
                            v8::String::Utf8Value value(isolate, str);
                            if (*value)
                            {
                                if (i > 0) result += " ";
                                result += *value;
                            }
                        }

                        if (handler->console_log_callback)
                        {
                            handler->console_log_callback(result);
                        } else
                        {
                            std::cout << "console.log: " << result << std::endl;
                        }
                    },
                    v8::External::New(isolate, this))
                .ToLocalChecked();

        console->Set(local_context, v8::String::NewFromUtf8(isolate, "log").ToLocalChecked(), log_function).Check();
        global->Set(local_context, v8::String::NewFromUtf8(isolate, "console").ToLocalChecked(), console).Check();
    }
};
