/*
*    task_service.hpp:
*
*    Copyright (C) 2015-2025 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#ifndef DAWN_PLAYER_TASK_SERVICE_HPP
#define DAWN_PLAYER_TASK_SERVICE_HPP

#include <coroutine>
#include <functional>
#include <thread>

namespace dawn_player {

struct task_service {
    virtual void post_task(std::function<void()>&& task) = 0;
    virtual std::thread::id get_thread_id() = 0;
    virtual ~task_service() {}
};

namespace impl {
    class switch_task_service_awaitor {
        task_service* tsk_service;
    public:
        switch_task_service_awaitor(task_service* service);
        bool await_ready();
        void await_resume();
        void await_suspend(std::coroutine_handle<> coro);
    };
}

impl::switch_task_service_awaitor switch_to_task_service(task_service* tsk_service);

} // namespace dawn_player

#endif
