//
// Created by maxim on 16.09.2024.
//
#pragma once
#include <v8.h>
#include <functional>

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
