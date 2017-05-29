/*
*    default_task_service.hpp:
*
*    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#ifndef DAWN_PLAYER_DEFAULT_TASK_SERVICE_HPP
#define DAWN_PLAYER_DEFAULT_TASK_SERVICE_HPP

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "task_service.hpp"

namespace dawn_player {
namespace impl
{

struct default_task_service_context {
    std::queue<std::function<void()>> task_queue;
    std::mutex task_queue_mtx;
    std::condition_variable task_queue_cv;
};

} // namespace impl

class default_task_service : public task_service {
public:
    default_task_service();
    virtual ~default_task_service();
    virtual void post_task(std::function<void()>&& task);
    virtual std::thread::id get_thread_id();
private:
    std::shared_ptr<impl::default_task_service_context> service_ctx;
    std::thread::id thread_id;
};

} // namespace dawn_player

#endif
