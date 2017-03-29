/*
*    default_task_service.cpp:
*
*    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#include "pch.h"

#include <thread>

#include "default_task_service.hpp"

namespace dawn_player {
namespace impl
{

void task_thread_proc(const std::shared_ptr<default_task_service_context>& service_ctx)
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lck(service_ctx->task_queue_mtx);
            service_ctx->task_queue_cv.wait(lck, [&service_ctx]() {
                return !service_ctx->task_queue.empty();
            });
            task = std::move(service_ctx->task_queue.front());
            service_ctx->task_queue.pop();
        }
        if (task == nullptr) {
            break;
        }
        task();
    }
}

} // namespace impl

default_task_service::default_task_service()
: service_ctx(std::make_shared<impl::default_task_service_context>())
{
    auto ctx = this->service_ctx;
    std::thread([ctx]() {
        impl::task_thread_proc(ctx);
    }).detach();
}

default_task_service::~default_task_service()
{
    this->post_task(nullptr);
}

void default_task_service::post_task(std::function<void()>&& task)
{
    {
        std::unique_lock<std::mutex> lck(this->service_ctx->task_queue_mtx);
        this->service_ctx->task_queue.push(std::move(task));
    }
    this->service_ctx->task_queue_cv.notify_one();
}

} // namespace dawn_player
