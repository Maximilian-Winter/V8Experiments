//
// Created by maxim on 14.09.2024.
//

#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "V8EngineContext.h"

class V8EngineManager
{
public:
    class V8EngineGuard
    {
    public:
        V8EngineGuard(V8EngineContext *engine, V8EngineManager *manager)
            : engine_(engine),
              manager_(manager),
              isolate_(engine->GetIsolate()),
            handle_scope(engine_->GetIsolate())
        {
        }

        ~V8EngineGuard()
        {
            // The destructor will automatically call the destructors of member variables
            // in the reverse order of their declaration in the class.
            manager_->returnEngine(engine_);
        }

        V8EngineGuard(const V8EngineGuard &) = delete;
        V8EngineGuard &operator=(const V8EngineGuard &) = delete;
        V8EngineGuard(V8EngineGuard &&) = delete;
        V8EngineGuard &operator=(V8EngineGuard &&) = delete;

        V8EngineContext* operator->() const
        {
            return engine_;
        }

        V8EngineContext& operator*() const
        {
            return *engine_;
        }


    private:
        V8EngineContext *engine_;
        V8EngineManager *manager_;
        v8::Isolate *isolate_;
        v8::HandleScope handle_scope;
    };

    explicit V8EngineManager(size_t pool_size = std::thread::hardware_concurrency())
    {
        for (size_t i = 0; i < pool_size; ++i)
        {
            auto engine = std::make_shared<V8EngineContext>(platform_context_);
            available_engines_.push(engine.get());
            engines_.push_back(engine);
        }
    }

    V8EngineGuard getEngine()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !available_engines_.empty(); });

        auto *engine = available_engines_.front();
        engine->Reset();
        available_engines_.pop();
        //v8::Isolate::Scope isolate_scope(engine->GetIsolate());

        return {engine, this};
    }

private:
    V8PlatformContext platform_context_;
    std::vector<std::shared_ptr<V8EngineContext> > engines_;
    std::queue<V8EngineContext *> available_engines_;
    std::mutex mutex_;
    std::condition_variable cv_;

    void returnEngine(V8EngineContext *engine)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        available_engines_.push(engine);
        cv_.notify_one();
    }
};
