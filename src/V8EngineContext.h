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

class JSValueWrapper {
public:
    enum class Type {
        Undefined,
        Null,
        Boolean,
        Number,
        String,
        Object,
        Array,
        Function
    };

    JSValueWrapper(v8::Isolate* isolate, std::shared_ptr<v8::Global<v8::Context>>& globalContext, v8::Local<v8::Value> value)
        : isolate_(isolate), global_context_(globalContext), persistent_(isolate, value) {
        type_ = GetValueType(value);
    }

    ~JSValueWrapper() {
        persistent_.Reset();
    }

    Type GetType() const { return type_; }

    template<typename T>
    T Get() const {

        v8::Local<v8::Context> context = global_context_->Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Value> value = persistent_.Get(isolate_);
        return ConvertToNative<T>(value, context);
    }
    template<typename T>
        T Get(std::string key) const {
        if (type_ != Type::Object && type_ != Type::Array) {
            throw std::runtime_error("Cannot get property on non-object value");
        }
        v8::Local<v8::Context> context = global_context_->Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Object> obj = persistent_.Get(isolate_).As<v8::Object>();
        v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
        v8::MaybeLocal<v8::Value> value = obj->Get(context, v8_key);
        if(value.IsEmpty())
        {
            throw std::runtime_error("Cannot find property on object");
        }
        return ConvertToNative<T>(value.ToLocalChecked(), context);
    }
    template<typename T>
    void Set(const std::string& key, const T& value) {
        if (type_ != Type::Object && type_ != Type::Array) {
            throw std::runtime_error("Cannot set property on non-object value");
        }

        v8::Local<v8::Context> context = global_context_->Get(isolate_);
        v8::Context::Scope context_scope(context);
        v8::Local<v8::Object> obj = persistent_.Get(isolate_).As<v8::Object>();
        v8::Local<v8::String> v8_key = v8::String::NewFromUtf8(isolate_, key.c_str()).ToLocalChecked();
        v8::Local<v8::Value> v8_value = ConvertToV8(value);
        if (obj->Set(context, v8_key, v8_value).IsNothing()) {
            throw std::runtime_error("Failed to set property: " + key);
        }
    }

    v8::Local<v8::Value> GetV8Value() const {
        return persistent_.Get(isolate_);
    }

    nlohmann::json ToJson() const {
        v8::HandleScope handle_scope(isolate_);
        v8::Local<v8::Context> context = isolate_->GetCurrentContext();
        v8::Local<v8::Value> value = persistent_.Get(isolate_);
        return V8ToJson(value, context);
    }

private:
    v8::Isolate* isolate_;
    std::shared_ptr<v8::Global<v8::Context>> global_context_;
    v8::Global<v8::Value> persistent_;
    Type type_;

    static Type GetValueType(v8::Local<v8::Value> value) {
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
    T ConvertToNative(v8::Local<v8::Value> value, v8::Local<v8::Context>& context) const {
        if constexpr (std::is_same_v<T, bool>) {
            return value->BooleanValue(isolate_);
        } else if constexpr (std::is_integral_v<T>) {
            return value->IntegerValue(context).FromMaybe(0);
        } else if constexpr (std::is_floating_point_v<T>) {
            return value->NumberValue(context).FromMaybe(0.0);
        } else if constexpr (std::is_same_v<T, std::string>) {
            v8::String::Utf8Value utf8(isolate_, value);
            return std::string(*utf8);
        } else if constexpr (std::is_same_v<T, std::vector<T>>) {
            std::vector<T> result;
            if (value->IsArray()) {
                v8::Local<v8::Array> array = value.As<v8::Array>();
                result.reserve(array->Length());
                for (uint32_t i = 0; i < array->Length(); ++i) {
                    v8::Local<v8::Value> element;
                    if (array->Get(context, i).ToLocal(&element)) {
                        result.push_back(ConvertToNative<T>(element, context));
                    }
                }
            }
            return result;
        } else {
            throw std::runtime_error("Unsupported type conversion");
        }
    }

    template<typename T>
    v8::Local<v8::Value> ConvertToV8(const T& value) const {
        if constexpr (std::is_same_v<T, bool>) {
            return v8::Boolean::New(isolate_, value);
        } else if constexpr (std::is_integral_v<T>) {
            return v8::Integer::New(isolate_, value);
        } else if constexpr (std::is_floating_point_v<T>) {
            return v8::Number::New(isolate_, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v8::String::NewFromUtf8(isolate_, value.c_str()).ToLocalChecked();
        } else if constexpr (std::is_same_v<T, std::vector<T>>) {
            v8::Local<v8::Array> array = v8::Array::New(isolate_, value.size());
            v8::Local<v8::Context> context = isolate_->GetCurrentContext();
            for (size_t i = 0; i < value.size(); ++i) {
                array->Set(context, i, ConvertToV8(value[i])).Check();
            }
            return array;
        } else {
            throw std::runtime_error("Unsupported type conversion");
        }
    }

    nlohmann::json V8ToJson(v8::Local<v8::Value> value, v8::Local<v8::Context>& context) const {
        if (value->IsNull()) {
            return nullptr;
        } else if (value->IsBoolean()) {
            return value->BooleanValue(isolate_);
        } else if (value->IsNumber()) {
            return value->NumberValue(context).FromMaybe(0.0);
        } else if (value->IsString()) {
            v8::String::Utf8Value utf8_value(isolate_, value);
            return std::string(*utf8_value);
        } else if (value->IsArray()) {
            v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(value);
            nlohmann::json j_array = nlohmann::json::array();
            for (uint32_t i = 0; i < array->Length(); ++i) {
                v8::MaybeLocal<v8::Value> maybe_element = array->Get(context, i);
                if (!maybe_element.IsEmpty()) {
                    j_array.push_back(V8ToJson(maybe_element.ToLocalChecked(), context));
                }
            }
            return j_array;
        } else if (value->IsObject()) {
            v8::Local<v8::Object> object = value.As<v8::Object>();
            nlohmann::json j_object = nlohmann::json::object();
            v8::Local<v8::Array> property_names = object->GetOwnPropertyNames(context).ToLocalChecked();
            for (uint32_t i = 0; i < property_names->Length(); ++i) {
                v8::Local<v8::Value> key = property_names->Get(context, i).ToLocalChecked();
                v8::MaybeLocal<v8::Value> maybe_value = object->Get(context, key);
                if (!maybe_value.IsEmpty()) {
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
                                                                 const auto *callback_ptr = static_cast<std::function<void(
                                                                     const v8::FunctionCallbackInfo<v8::Value> &)> *>(
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
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
    }

    [[nodiscard]] std::shared_ptr<v8::Platform> GetPlatform() const
    {
        return platform;
    }

private:
    std::shared_ptr<v8::Platform> platform;
};

class V8EngineContext
{
    std::function<void(const std::string &)> console_log_callback;
    std::shared_ptr<v8::Platform> platform;
    v8::Isolate *isolate;
    std::shared_ptr<v8::Global<v8::Context>> context;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
    V8CallbackManager callback_manager_;
public:
    explicit V8EngineContext(const V8PlatformContext &platform)
        : platform(platform.GetPlatform()), context(std::make_shared<v8::Global<v8::Context>>())
    {
        allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(v8::ArrayBuffer::Allocator::NewDefaultAllocator());

        v8::Isolate::CreateParams create_params;
        create_params.array_buffer_allocator = allocator.get();

        // Create the isolate
        isolate = v8::Isolate::New(create_params);
        Reset();
    }

    ~V8EngineContext()
    {
        // Dispose of persistent handles first
        context->Reset();
        // Dispose of the isolate
        isolate->Dispose();
    }

    void Reset()
    {
        ClearCallbacks();
        context->Reset();

        // Create a stack-allocated handle scope
        v8::HandleScope handle_scope(isolate);

        // Create a new context
        v8::Local<v8::Context> local_context = v8::Context::New(isolate);

        // Create a persistent handle from the local handle
        context->Reset(isolate, local_context);
        InitializeConsole();
    }

    void SetConsoleLogCallback(std::function<void(const std::string &)> callback)
    {
        console_log_callback = std::move(callback);
    }

    void RegisterCallback(const std::string& name, V8CallbackManager::JavascriptCallback callback)
    {
        callback_manager_.RegisterCallback(name, std::move(callback));
    }

    void ClearCallbacks()
    {
        callback_manager_.ClearCallbacks();
    }

   std::unique_ptr<JSValueWrapper> V8EngineContext::ExecuteJS(const std::string& js_code) {
       const v8::Local<v8::Context> local_context = GetLocalContext();
        v8::Context::Scope context_scope(local_context);
       const v8::TryCatch try_catch(isolate);
       const v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
        v8::MaybeLocal<v8::Script> maybe_script = v8::Script::Compile(local_context, source);

        if (maybe_script.IsEmpty()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error compiling JS code: " << *error << std::endl;
            return nullptr;
        }

        v8::Local<v8::Script> script = maybe_script.ToLocalChecked();

        callback_manager_.ExposeCallbacks(isolate, local_context);
        v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "JavaScript error: " << *error << std::endl;
            return nullptr;
        }

        if (maybe_result.IsEmpty()) {
            return nullptr;
        }

        return std::make_unique<JSValueWrapper>(isolate, context, maybe_result.ToLocalChecked());
    }

    std::future<std::unique_ptr<JSValueWrapper>> ExecuteJSAsync(const std::string& js_code) {
        return std::async(std::launch::deferred, [this, js_code]() {

            v8::Local<v8::Context> local_context = context->Get(isolate);
            v8::Context::Scope context_scope(local_context);

            const v8::TryCatch try_catch(isolate);
            const v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
            v8::MaybeLocal<v8::Script> maybe_script = v8::Script::Compile(local_context, source);

            if (maybe_script.IsEmpty()) {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "Error compiling JS code: " << *error << std::endl;
                return std::unique_ptr<JSValueWrapper>(nullptr);
            }

            v8::Local<v8::Script> script = maybe_script.ToLocalChecked();

            callback_manager_.ExposeCallbacks(isolate, local_context);
            v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

            if (try_catch.HasCaught()) {
                v8::String::Utf8Value error(isolate, try_catch.Exception());
                std::cerr << "JavaScript error: " << *error << std::endl;
                return std::unique_ptr<JSValueWrapper>(nullptr);
            }

            if (maybe_result.IsEmpty()) {
                return std::unique_ptr<JSValueWrapper>(nullptr);
            }

            return std::make_unique<JSValueWrapper>(isolate, context, maybe_result.ToLocalChecked());
        });
    }

    std::unique_ptr<JSValueWrapper> CreateJSValue(const std::string& js_code) {
        const v8::Local<v8::Context> local_context = GetLocalContext();
        v8::Context::Scope context_scope(local_context);
        const std::string final_code = "(" + js_code + ")";
        const v8::TryCatch try_catch(isolate);
        const v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, final_code.c_str()).ToLocalChecked();

        v8::MaybeLocal<v8::Script> maybe_script = v8::Script::Compile(local_context, source);
        if (maybe_script.IsEmpty()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error compiling JS code: " << *error << std::endl;
            return nullptr;
        }

        v8::Local<v8::Script> script = maybe_script.ToLocalChecked();
        v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

        if (maybe_result.IsEmpty()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error executing JS code: " << *error << std::endl;
            return nullptr;
        }

        v8::Local<v8::Value> result = maybe_result.ToLocalChecked();
        return std::make_unique<JSValueWrapper>(isolate, context, result);
    }

    std::unique_ptr<JSValueWrapper> CallJSFunction(const std::string& function_name, std::vector<v8::Local<v8::Value>>& args) {
        const v8::Local<v8::Context> local_context = GetLocalContext();
        v8::Context::Scope context_scope(local_context);

        const v8::TryCatch try_catch(isolate);
        const v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate, function_name.c_str()).ToLocalChecked();
        v8::Local<v8::Value> func_val;
        if (!local_context->Global()->Get(local_context, func_name).ToLocal(&func_val) || !func_val->IsFunction()) {
            std::cerr << "Function " << function_name << " not found or is not a function" << std::endl;
            return nullptr;
        }

        const v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);

        const v8::MaybeLocal<v8::Value> result = func->Call(local_context, v8::Undefined(isolate), static_cast<int>(args.size()), args.data());

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error calling function " << function_name << ": " << *error << std::endl;
            return nullptr;
        }
        v8::Local<v8::Value> result_value;
        if (!result.ToLocal(&result_value)) {
            return nullptr;
        }
        return std::make_unique<JSValueWrapper>(isolate, context, result_value);
    }

    [[nodiscard]] v8::Local<v8::Context> GetLocalContext() const { return context->Get(isolate); }

    [[nodiscard]] v8::Isolate *GetIsolate() const { return isolate; }

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
                        V8EngineContext *handler = static_cast<V8EngineContext *>(v8::External::Cast(*data)->Value());
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
