/*
 *    task_service.cpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include <memory>

#include "task_service.hpp"

namespace dawn_player {

std::future<void> switch_to_task_service(task_service* tsk_service)
{
    auto p = std::make_shared<std::promise<void>>();
    tsk_service->post_task([p]() {
        p->set_value();
    });
    return p->get_future();
}

} // namespace dawn_player
