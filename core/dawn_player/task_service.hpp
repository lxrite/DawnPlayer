/*
*    task_service.hpp:
*
*    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#ifndef DAWN_PLAYER_TASK_SERVICE_HPP
#define DAWN_PLAYER_TASK_SERVICE_HPP

#include <functional>

namespace dawn_player {

struct task_service {
    virtual void post_task(std::function<void()>&& task) = 0;
    virtual ~task_service() {}
};

} // namespace dawn_player

#endif
