/*
 *    task_service.cpp:
 *
 *    Copyright (C) 2015-2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include <memory>

#include "task_service.hpp"

namespace dawn_player {

namespace impl {

switch_task_service_awaitor::switch_task_service_awaitor(task_service* service)
    : tsk_service(service)
{}

bool switch_task_service_awaitor::await_ready()
{
    return std::this_thread::get_id() == this->tsk_service->get_thread_id();
}

void switch_task_service_awaitor::await_resume()
{
}

void switch_task_service_awaitor::await_suspend(std::coroutine_handle<> coro)
{
    this->tsk_service->post_task([coro]() {
        coro.resume();
    });
}

} // namespace impl

impl::switch_task_service_awaitor switch_to_task_service(task_service* tsk_service)
{
    return impl::switch_task_service_awaitor{ tsk_service };
}

} // namespace dawn_player
