#pragma once
#include <string>
#include <memory>
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>


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
    }

    ~V8Handler() {
        context.Reset();
        isolate->Dispose();
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
    }

    [[nodiscard]] bool ExecuteJS(const std::string& js_code) const
    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        const v8::Local<v8::Context> local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);

        const v8::TryCatch try_catch(isolate);
        const v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
        const v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
        v8::MaybeLocal<v8::Value> result = script->Run(local_context);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "JavaScript error: " << *error << std::endl;
            return false;
        }

        return true;
    }

    v8::Local<v8::Value> CreateJSObject(const std::string& js_code) {
        v8::EscapableHandleScope handle_scope(isolate);
        v8::Local<v8::Context> local_context = context.Get(isolate);
        v8::Context::Scope context_scope(local_context);

        v8::TryCatch try_catch(isolate);
        v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, js_code.c_str()).ToLocalChecked();
        v8::Local<v8::Script> script = v8::Script::Compile(local_context, source).ToLocalChecked();
        v8::MaybeLocal<v8::Value> maybe_result = script->Run(local_context);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error creating JS object: " << *error << std::endl;
            return v8::Null(isolate);
        }

        v8::Local<v8::Value> result;
        if (!maybe_result.ToLocal(&result)) {
            std::cerr << "Failed to get local value when creating JS object" << std::endl;
            return v8::Null(isolate);
        }

        return handle_scope.Escape(result);
    }

       v8::MaybeLocal<v8::Value> CallJSFunction(const std::string& function_name, const v8::Local<v8::Value>& arg) {
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

        std::cout << "Function " << function_name << " found" << std::endl;

        v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_val);
        v8::Local<v8::Value> args[] = { arg };

        std::cout << "Argument type: " << *v8::String::Utf8Value(isolate, arg->TypeOf(isolate)) << std::endl;
        if (arg->IsObject()) {
            v8::Local<v8::Object> obj = arg.As<v8::Object>();
            v8::Local<v8::Array> props = obj->GetOwnPropertyNames(local_context).ToLocalChecked();
            std::cout << "Object properties:" << std::endl;
            for (uint32_t i = 0; i < props->Length(); ++i) {
                v8::Local<v8::Value> key = props->Get(local_context, i).ToLocalChecked();
                v8::Local<v8::Value> value = obj->Get(local_context, key).ToLocalChecked();
                std::cout << "  " << *v8::String::Utf8Value(isolate, key) << ": " << *v8::String::Utf8Value(isolate, value) << std::endl;
            }
        }

        std::cout << "Calling function " << function_name << "..." << std::endl;
        v8::MaybeLocal<v8::Value> result = func->Call(local_context, v8::Undefined(isolate), 1, args);

        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error(isolate, try_catch.Exception());
            std::cerr << "Error calling function " << function_name << ": " << *error << std::endl;
            return {};
        }

        std::cout << "Function " << function_name << " called successfully" << std::endl;
        return result;
    }


    [[nodiscard]] v8::Isolate* GetIsolate() const { return isolate; }
    v8::Local<v8::Context> GetContext() { return context.Get(isolate); }

private:
    std::unique_ptr<v8::Platform> platform;
    v8::Isolate* isolate;
    v8::Global<v8::Context> context;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
};