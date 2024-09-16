//
// Created by maxim on 16.09.2024.
//
#pragma once
#include "AsyncExecutor.h"
#include <future>
#include <v8.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

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
