//
// Created by maxim on 16.09.2024.
//
#pragma once
#include <v8.h>
#include <libplatform/libplatform.h>

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
