#pragma once
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>
#include <functional>

#pragma once
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>
#include <functional>
#include <vector>

class V8Handler {
public:
    V8Handler();
    ~V8Handler();
    [[nodiscard]] std::string ExecuteJS(const std::string& js_code) const;

    // New methods
    void DefineJSFunction(const std::string& function_name, const std::string& js_code);
    v8::MaybeLocal<v8::Value> CallJSFunction(const std::string& function_name, std::vector<v8::Local<v8::Value>>& args);
    v8::Local<v8::Value> CreateJSObject(const std::string& js_code);
    void RegisterCppCallback(const std::string& name, const std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>& callback);

    // Helper method to convert C++ values to v8::Local<v8::Value>
    v8::Local<v8::Value> ConvertToV8Value(const std::string& value);
    v8::Local<v8::Value> ConvertToV8Value(double value);
    v8::Local<v8::Value> ConvertToV8Value(bool value);
    [[nodiscard]] v8::Isolate * GetIsolate() const
    {
        return isolate;
    }
private:
    std::unique_ptr<v8::Platform> platform;
    v8::Isolate* isolate;
    v8::Global<v8::Context> context;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
};

inline V8Handler::V8Handler() {
    v8::V8::InitializeICUDefaultLocation(nullptr);
    v8::V8::InitializeExternalStartupData(nullptr);
    platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    // Create a new ArrayBuffer::Allocator
    allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(v8::ArrayBuffer::Allocator::NewDefaultAllocator());

    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = allocator.get();

    isolate = v8::Isolate::New(create_params);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);

    const v8::Local<v8::Context> local_context = v8::Context::New(isolate);
    context.Reset(isolate, local_context);
}

inline V8Handler::~V8Handler() {
    context.Reset();
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
}

inline std::string V8Handler::ExecuteJS(const std::string& js_code) const
{
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> local_context = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_context);

    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
    v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
    v8::Local<v8::Value> result = script->Run(local_context).ToLocalChecked();
    v8::String::Utf8Value utf8(isolate, result);
    return *utf8;
}

inline void V8Handler::DefineJSFunction(const std::string& function_name, const std::string& js_code) {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> local_context = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_context);

    std::string full_js_code = function_name + " = " + js_code;
    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, full_js_code.c_str()).ToLocalChecked();
    v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
    script->Run(local_context).ToLocalChecked();
}

inline v8::MaybeLocal<v8::Value> V8Handler::CallJSFunction(const std::string& function_name, std::vector<v8::Local<v8::Value>>& args) {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> local_context = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_context);

    v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate, function_name.c_str()).ToLocalChecked();
    v8::Local<v8::Value> func_val;
    if (!local_context->Global()->Get(local_context, func_name).ToLocal(&func_val)) {
        return v8::MaybeLocal<v8::Value>();
    }

    if (!func_val->IsFunction()) {
        return v8::MaybeLocal<v8::Value>();
    }
    v8::Local<v8::Value> val;
    v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);
    return func->Call(isolate, local_context, val, static_cast<int>(args.size()), args.data());
}

inline v8::Local<v8::Value> V8Handler::CreateJSObject(const std::string& js_code) {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> local_context = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_context);

    v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
    v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
    return script->Run(local_context).ToLocalChecked();
}

inline void V8Handler::RegisterCppCallback(const std::string& name, const std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>& callback) {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> local_context = v8::Local<v8::Context>::New(isolate, context);
    v8::Context::Scope context_scope(local_context);

    v8::Local<v8::String> func_name = v8::String::NewFromUtf8(isolate, name.c_str()).ToLocalChecked();
    v8::Local<v8::FunctionTemplate> func_template = v8::FunctionTemplate::New(isolate,
        [](const v8::FunctionCallbackInfo<v8::Value>& args) {
            v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
            auto* callback = static_cast<std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>*>(external->Value());
            (*callback)(args);
        },
        v8::External::New(isolate, const_cast<std::function<void(const v8::FunctionCallbackInfo<v8::Value>&)>*>(&callback))
    );

    local_context->Global()->Set(local_context, func_name, func_template->GetFunction(local_context).ToLocalChecked()).Check();
}

// Helper methods to convert C++ values to v8::Local<v8::Value>
inline v8::Local<v8::Value> V8Handler::ConvertToV8Value(const std::string& value) {
    return v8::String::NewFromUtf8(isolate, value.c_str()).ToLocalChecked();
}

inline v8::Local<v8::Value> V8Handler::ConvertToV8Value(double value) {
    return v8::Number::New(isolate, value);
}

inline v8::Local<v8::Value> V8Handler::ConvertToV8Value(bool value) {
    return v8::Boolean::New(isolate, value);
}