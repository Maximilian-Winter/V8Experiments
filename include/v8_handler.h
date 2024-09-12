#pragma once
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>

class V8Handler {
public:
    V8Handler();
    ~V8Handler();
    [[nodiscard]] std::string ExecuteJS(const std::string& js_code) const;

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
