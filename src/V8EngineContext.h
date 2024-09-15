#pragma once
#include <future>
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <utility>
using json = nlohmann::json;

class AsyncExecutor
{
public:
    using TaskFunction = std::function<void()>;
    virtual ~AsyncExecutor() = default;
    virtual void ExecuteAsync(const TaskFunction &task_function) = 0;
};

class JSValueWrapper
{
public:
    enum class Type
    {
        Undefined,
        Null,
        Boolean,
        Number,
        String,
        Object,
        Array,
        Function
    };

    JSValueWrapper(v8::Isolate *isolate, const std::shared_ptr<v8::Global<v8::Context> > &globalContext,
                   v8::Local<v8::Value> value, std::shared_ptr<AsyncExecutor> async_executor)
        : isolate_(isolate), global_context_(globalContext), persistent_(isolate, value), async_executor_(std::move(async_executor))
    {
        type_ = GetValueType(value);
    }

    ~JSValueWrapper()
    {
        std::promise<void> promise;
        std::future<void> future = promise.get_future();

        async_executor_->ExecuteAsync([this, &promise]()
        {
            if (!persistent_.IsEmpty()) {
                persistent_.Reset();
            }
            promise.set_value();
        });
        future.get();
        async_executor_.reset();

    }

    [[nodiscard]] Type GetType() const { return type_; }

    template<typename T>
    T Get() const
    {
        std::promise<T> promise;
        std::future<T> future = promise.get_future();

        async_executor_->ExecuteAsync([this, &promise]()
        {
            v8::HandleScope handle_scope(isolate_);
            v8::Local<v8::Context> context = global_context_->Get(isolate_);
            v8::Context::Scope context_scope(context);
            v8::Local<v8::Value> value = persistent_.Get(isolate_);
            promise.set_value(ConvertToNative<T>(value, context));
        });
        return future.get();
    }

    template<typename T>
    T Get(std::string key) const
    {
        if (type_ != Type::Object && type_ != Type::Array)
        {
            throw std::runtime_error("Cannot get property on non-object value");
        }
        std::promise<T> promise;
        std::future<T> future = promise.get_future();

        async_executor_->ExecuteAsync([this, &promise, key]()
        {
            v8::HandleScope handle_scope(isolate_);
            v8::Local<v8::Context> context = global_context_->Get(isolate_);
            v8::Context::Scope context_scope(context);
            v8::Local<v8::Object> obj = persistent_.Get(isolate_).As<v8::Object>();
            v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
            v8::MaybeLocal<v8::Value> value = obj->Get(context, v8_key);
            if (value.IsEmpty())
            {
                throw std::runtime_error("Cannot find property on object");
            }
            promise.set_value(ConvertToNative<T>(value.ToLocalChecked(), context));
        });
        return future.get();
    }

    template<typename T>
    void Set(const std::string &key, const T &value)
    {
        if (type_ != Type::Object && type_ != Type::Array)
        {
            throw std::runtime_error("Cannot set property on non-object value");
        }
        std::promise<bool> promise;
        std::future<bool> future = promise.get_future();

        async_executor_->ExecuteAsync([this, &promise, key, value]()
        {
            v8::HandleScope handle_scope(isolate_);
            v8::Local<v8::Context> context = global_context_->Get(isolate_);
            v8::Context::Scope context_scope(context);
            v8::Local<v8::Object> obj = persistent_.Get(isolate_).As<v8::Object>();
            v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
            v8::Local<v8::Value> v8_value = ConvertToV8(value);
            if (obj->Set(context, v8_key, v8_value).IsNothing())
            {
                throw std::runtime_error("Failed to set property: " + key);
            }
            promise.set_value(true);
        });
        future.get();
    }

    [[nodiscard]] v8::Local<v8::Value> GetV8ValueInternal() const
    {
        return persistent_.Get(isolate_);
    }

    [[nodiscard]] nlohmann::json ToJson() const
    {
        std::promise<nlohmann::json> promise;
        std::future<nlohmann::json> future = promise.get_future();

        async_executor_->ExecuteAsync([this, &promise]()
        {
            v8::HandleScope handle_scope(isolate_);
            v8::Local<v8::Context> context = global_context_->Get(isolate_);
            v8::Context::Scope context_scope(context);
            v8::Local<v8::Value> value = persistent_.Get(isolate_);
            promise.set_value(V8ToJson(value, context));
        });
        return future.get();
    }

private:
    std::shared_ptr<AsyncExecutor> async_executor_;
    v8::Isolate *isolate_;
    std::shared_ptr<v8::Global<v8::Context> > global_context_;
    v8::Global<v8::Value> persistent_;
    Type type_;

    static Type GetValueType(v8::Local<v8::Value> value)
    {
        if (value->IsUndefined()) return Type::Undefined;
        if (value->IsNull()) return Type::Null;
        if (value->IsBoolean()) return Type::Boolean;
        if (value->IsNumber()) return Type::Number;
        if (value->IsString()) return Type::String;
        if (value->IsArray()) return Type::Array;
        if (value->IsFunction()) return Type::Function;
        if (value->IsObject()) return Type::Object;
        return Type::Undefined;
    }

    template<typename T>
    T ConvertToNative(v8::Local<v8::Value> value, v8::Local<v8::Context> &context) const
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            return value->BooleanValue(isolate_);
        } else if constexpr (std::is_integral_v<T>)
        {
            return value->IntegerValue(context).FromMaybe(0);
        } else if constexpr (std::is_floating_point_v<T>)
        {
            return value->NumberValue(context).FromMaybe(0.0);
        } else if constexpr (std::is_same_v<T, std::string>)
        {
            v8::String::Utf8Value utf8(isolate_, value);
            return std::string(*utf8);
        } else if constexpr (std::is_same_v<T, std::vector<T> >)
        {
            std::vector<T> result;
            if (value->IsArray())
            {
                v8::Local<v8::Array> array = value.As<v8::Array>();
                result.reserve(array->Length());
                for (uint32_t i = 0; i < array->Length(); ++i)
                {
                    v8::Local<v8::Value> element;
                    if (array->Get(context, i).ToLocal(&element))
                    {
                        result.push_back(ConvertToNative<T>(element, context));
                    }
                }
            }
            return result;
        } else
        {
            throw std::runtime_error("Unsupported type conversion");
        }
    }

    template<typename T>
    v8::Local<v8::Value> ConvertToV8(const T &value) const
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            return v8::Boolean::New(isolate_, value);
        } else if constexpr (std::is_integral_v<T>)
        {
            return v8::Integer::New(isolate_, value);
        } else if constexpr (std::is_floating_point_v<T>)
        {
            return v8::Number::New(isolate_, value);
        } else if constexpr (std::is_same_v<T, std::string>)
        {
            return v8::String::NewFromUtf8(isolate_, value.c_str()).ToLocalChecked();
        } else if constexpr (std::is_same_v<T, std::vector<T> >)
        {
            v8::Local<v8::Array> array = v8::Array::New(isolate_, value.size());
            v8::Local<v8::Context> context = isolate_->GetCurrentContext();
            for (size_t i = 0; i < value.size(); ++i)
            {
                array->Set(context, i, ConvertToV8(value[i])).Check();
            }
            return array;
        } else
        {
            throw std::runtime_error("Unsupported type conversion");
        }
    }

    nlohmann::json V8ToJson(v8::Local<v8::Value> value, v8::Local<v8::Context> &context) const
    {
        if (value->IsNull())
        {
            return nullptr;
        } else if (value->IsBoolean())
        {
            return value->BooleanValue(isolate_);
        } else if (value->IsNumber())
        {
            return value->NumberValue(context).FromMaybe(0.0);
        } else if (value->IsString())
        {
            v8::String::Utf8Value utf8_value(isolate_, value);
            return std::string(*utf8_value);
        } else if (value->IsArray())
        {
            v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(value);
            nlohmann::json j_array = nlohmann::json::array();
            for (uint32_t i = 0; i < array->Length(); ++i)
            {
                v8::MaybeLocal<v8::Value> maybe_element = array->Get(context, i);
                if (!maybe_element.IsEmpty())
                {
                    j_array.push_back(V8ToJson(maybe_element.ToLocalChecked(), context));
                }
            }
            return j_array;
        } else if (value->IsObject())
        {
            v8::Local<v8::Object> object = value.As<v8::Object>();
            nlohmann::json j_object = nlohmann::json::object();
            v8::Local<v8::Array> property_names = object->GetOwnPropertyNames(context).ToLocalChecked();
            for (uint32_t i = 0; i < property_names->Length(); ++i)
            {
                v8::Local<v8::Value> key = property_names->Get(context, i).ToLocalChecked();
                v8::MaybeLocal<v8::Value> maybe_value = object->Get(context, key);
                if (!maybe_value.IsEmpty())
                {
                    v8::String::Utf8Value utf8_key(isolate_, key);
                    j_object[std::string(*utf8_key)] = V8ToJson(maybe_value.ToLocalChecked(), context);
                }
            }
            return j_object;
        }
        return nullptr;
    }
};

class V8CallbackManager
{
public:
    using JavascriptCallback = std::function<void(const v8::FunctionCallbackInfo<v8::Value> &)>;

    explicit V8CallbackManager()
    = default;

    void RegisterCallback(const std::string &name,
                          JavascriptCallback callback)
    {
        callbacks_[name] = std::move(callback);
    }

    void ExposeCallbacks(v8::Isolate *isolate, const v8::Local<v8::Context> context) const
    {
        const v8::Local<v8::Object> global = context->Global();

        for (const auto &[name, callback]: callbacks_)
        {
            v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
            v8::Local<v8::Function> func = v8::Function::New(context,
                                                             [](const v8::FunctionCallbackInfo<v8::Value> &args)
                                                             {
                                                                 v8::Isolate *isolate_ = args.GetIsolate();
                                                                 v8::HandleScope handle_scope(isolate_);
                                                                 const v8::Local<v8::External> data = v8::Local<
                                                                     v8::External>::Cast(args.Data());
                                                                 const auto *callback_ptr = static_cast<std::function<
                                                                         void(
                                                                             const v8::FunctionCallbackInfo<v8::Value> &
                                                                         )>
                                                                     *>(
                                                                     data->Value());
                                                                 (*callback_ptr)(args);
                                                             },
                                                             v8::External::New(
                                                                 isolate, const_cast<std::function<void(
                                                                     const v8::FunctionCallbackInfo<v8::Value> &)> *>(&
                                                                     callback))
            ).ToLocalChecked();

            global->Set(context, func_name, func).Check();
        }
    }

    void ClearCallbacks()
    {
        callbacks_.clear();
    }

private:
    std::unordered_map<std::string, std::function<void(const v8::FunctionCallbackInfo<v8::Value> &)> > callbacks_;
};

class V8PlatformContext
{
public:
    V8PlatformContext()
    {
        v8::V8::InitializeICUDefaultLocation(nullptr);
        v8::V8::InitializeExternalStartupData(nullptr);
        platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(platform.get());
        v8::V8::Initialize();
    }

    ~V8PlatformContext()
    {
        // Dispose of V8
        //v8::V8::Dispose();
        //v8::V8::DisposePlatform();
    }

    [[nodiscard]] std::shared_ptr<v8::Platform> GetPlatform() const
    {
        return platform;
    }

private:
    std::shared_ptr<v8::Platform> platform;
};

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
