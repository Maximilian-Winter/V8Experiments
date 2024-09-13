#pragma once
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>

class JSObjectWrapper {
public:
    JSObjectWrapper(v8::Isolate* isolate, v8::Local<v8::Object> object)
        : isolate_(isolate), persistent_(isolate, object) {}

    ~JSObjectWrapper() {
        persistent_.Reset();
    }

    template<typename T>
    T Get(const std::string& key) {
        v8::HandleScope handle_scope(isolate_);
        v8::Local<v8::Context> context = isolate_->GetCurrentContext();
        v8::Local<v8::Object> obj = persistent_.Get(isolate_);
        v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
        v8::Local<v8::Value> value = obj->Get(context, v8_key).ToLocalChecked();
        return ConvertToNative<T>(value, context);
    }

    template<typename T>
    void Set(const std::string& key, const T& value) {
        v8::HandleScope handle_scope(isolate_);
        v8::Local<v8::Context> context = isolate_->GetCurrentContext();
        v8::Local<v8::Object> obj = persistent_.Get(isolate_);
        v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
        v8::Local<v8::Value> v8_value = ConvertToV8(value);
        obj->Set(context, v8_key, v8_value).Check();
    }

    v8::Local<v8::Value> GetV8Object() {
        return persistent_.Get(isolate_);
    }

private:
    v8::Isolate* isolate_;
    v8::Global<v8::Object> persistent_;

    template<typename T>
    T ConvertToNative(v8::Local<v8::Value> value, v8::Local<v8::Context> context) {
        if constexpr (std::is_same_v<T, int>) {
            return value->Int32Value(context).ToChecked();
        } else if constexpr (std::is_same_v<T, double>) {
            return value->NumberValue(context).ToChecked();
        } else if constexpr (std::is_same_v<T, std::string>) {
            v8::String::Utf8Value utf8(isolate_, value);
            return std::string(*utf8);
        } else {
            throw std::runtime_error("Unsupported type conversion");
        }
    }

    template<typename T>
    v8::Local<v8::Value> ConvertToV8(const T& value) {
        if constexpr (std::is_same_v<T, int>) {
            return v8::Integer::New(isolate_, value);
        } else if constexpr (std::is_same_v<T, double>) {
            return v8::Number::New(isolate_, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v8::String::NewFromUtf8(isolate_, value.c_str()).ToLocalChecked();
        } else {
            throw std::runtime_error("Unsupported type conversion");
        }
    }
};

class CallbackManager {
public:
    explicit CallbackManager(v8::Isolate* isolate) : isolate_(isolate) {}

    void RegisterCallback(const std::string& name, std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)> callback) {
        callbacks_[name] = std::move(callback);
    }

    void ExposeCallbacks(v8::Local<v8::Context> context) {
        v8::HandleScope handle_scope(isolate_);
        v8::Local<v8::Object> global = context->Global();

        for (const auto& [name, callback] : callbacks_) {
            v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate_, name.c_str()).ToLocalChecked();
            v8::Local<v8::Function> func = v8::Function::New(context,
                [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                    v8::Isolate* isolate = args.GetIsolate();
                    v8::HandleScope handle_scope(isolate);
                    v8::Local<v8::External> data = v8::Local<v8::External>::Cast(args.Data());
                    auto* callback_ptr = static_cast<std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>*>(data->Value());
                    (*callback_ptr)(args);
                },
                v8::External::New(isolate_, const_cast<std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>*>(&callback))
            ).ToLocalChecked();

            global->Set(context, func_name, func).Check();
        }
    }

private:
    v8::Isolate* isolate_;
    std::unordered_map<std::string, std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>> callbacks_;
};


class V8Handler {
public:
    V8Handler() {
        v8::V8::InitializeICUDefaultLocation(nullptr);
        v8::V8::InitializeExternalStartupData(nullptr);
        platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(platform.get());
        v8::V8::Initialize();

        allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(v8::ArrayBuffer::Allocator::NewDefaultAllocator());

        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = allocator.get();

        isolate = v8::Isolate::New(create_params);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);

        context.Reset(isolate, v8::Context::New(isolate));

        callback_manager = std::make_unique<CallbackManager>(isolate);

        InitializeConsole();
    }

    ~V8Handler() {
        context.Reset();
        isolate->Dispose();
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
    }

    bool ExecuteJS(const std::string& js_code) {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);

        callback_manager->ExposeCallbacks(local_context);

        v8::TryCatch try_catch(isolate);
        v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
        v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
        v8::MaybeLocal<v8::Value> result = script->Run(local_context);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "JavaScript error: " << *error << std::endl;
            return false;
        }

        return true;
    }

    std::unique_ptr<JSObjectWrapper> CreateJSObject(const std::string& js_code) {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);

        v8::TryCatch try_catch(isolate);
        v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
        v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
        v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error creating JS object: " << *error << std::endl;
            return nullptr;
        }

        v8::Local<v8::Value> result;
        if (!maybe_result.ToLocal(&result) || !result->IsObject()) {
            std::cerr << "Failed to create JavaScript object" << std::endl;
            return nullptr;
        }

        return std::make_unique<JSObjectWrapper>(isolate, result.As<v8::Object>());
    }

    v8::MaybeLocal<v8::Value> CallJSFunction(const std::string& function_name, const v8::Local<v8::Value>& arg) const
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);

        v8::TryCatch try_catch(isolate);
        v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate, function_name.c_str()).ToLocalChecked();
        v8::Local<v8::Value> func_val;
        if (!local_context->Global()->Get(local_context, func_name).ToLocal(&func_val) || !func_val->IsFunction()) {
            std::cerr << "Function " << function_name << " not found or is not a function" << std::endl;
            return {};
        }

        v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);
        v8::Local<v8::Value> args[] = { arg };

        v8::MaybeLocal<v8::Value> result = func->Call(local_context, v8::Undefined(isolate), 1, args);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error calling function " << function_name << ": " << *error << std::endl;
            return {};
        }
        return result;
    }
    void RegisterCallback(const std::string& name, std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)> callback) {
        callback_manager->RegisterCallback(name, std::move(callback));
    }

    [[nodiscard]] v8::Local<v8::Context> GetContext() const { return context.Get(isolate); }
    void SetConsoleLogCallback(std::function<void(const std::string&)> callback) {
        console_log_callback_ = std::move(callback);
    }
    v8::Isolate* GetIsolate() const { return isolate; }
private:

    void InitializeConsole() {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);

        v8::Local<v8::Object> global = local_context->Global();

        v8::Local<v8::Object> console = v8::Object::New(isolate);
        v8::Local<v8::Function> log_function = v8::Function::New(
            local_context,
            [](const v8::FunctionCallbackInfo<v8::Value>& args) {
                v8::Isolate* isolate = args.GetIsolate();
                v8::HandleScope handle_scope(isolate);
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Value> data = args.Data();
                V8Handler* handler = static_cast<V8Handler*>(v8::External::Cast(*data)->Value());

                std::string result;
                for (int i = 0; i < args.Length(); i++) {
                    v8::Local<v8::Value> arg = args[i];
                    v8::String::Utf8Value value(isolate, arg);
                    if (*value) {
                        if (i > 0) result += " ";
                        result += *value;
                    }
                }

                if (handler->console_log_callback_) {
                    handler->console_log_callback_(result);
                } else {
                    std::cout << "console.log: " << result << std::endl;
                }
            },
            v8::External::New(isolate, this)
        ).ToLocalChecked();

        console->Set(local_context, v8::String::NewFromUtf8(isolate, "log").ToLocalChecked(), log_function).Check();
        global->Set(local_context, v8::String::NewFromUtf8(isolate, "console").ToLocalChecked(), console).Check();
    }

    std::function<void(const std::string&)> console_log_callback_;
    std::unique_ptr<v8::Platform> platform;
    v8::Isolate* isolate;
    v8::Global<v8::Context> context;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
    std::unique_ptr<CallbackManager> callback_manager;
};