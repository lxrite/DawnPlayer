/*
 *    sync_wait.hpp:
 *
 *    Copyright (C) 2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_COROUTINE_SYNC_WAIT_HPP
#define DAWN_PLAYER_COROUTINE_SYNC_WAIT_HPP

#include <coroutine>
#include <future>
#include <type_traits>

#include "task.hpp"

namespace dawn_player::coroutine::impl {
struct as_coroutine;
}

// Enable the use of std::future<T> as a coroutine type
// by using a std::promise<T> as the promise type.
template<typename T, typename... Args>
    requires(!std::is_void_v<T> && !std::is_reference_v<T>)
struct std::coroutine_traits<std::future<T>, dawn_player::coroutine::impl::as_coroutine, Args...>
{
    struct promise_type : std::promise<T>
    {
        std::future<T> get_return_object() noexcept
        {
            return this->get_future();
        }

        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_never final_suspend() const noexcept { return {}; }

        void return_value(const T& value)
            noexcept(std::is_nothrow_copy_constructible_v<T>)
        {
            this->set_value(value);
        }

        void return_value(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            this->set_value(std::move(value));
        }

        void unhandled_exception() noexcept
        {
            this->set_exception(std::current_exception());
        }
    };
};

// Same for std::future<void>.
template<typename... Args>
struct std::coroutine_traits<std::future<void>, dawn_player::coroutine::impl::as_coroutine, Args...>
{
    struct promise_type : std::promise<void>
    {
        std::future<void> get_return_object() noexcept
        {
            return this->get_future();
        }

        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_never final_suspend() const noexcept { return {}; }

        void return_void() noexcept
        {
            this->set_value();
        }

        void unhandled_exception() noexcept
        {
            this->set_exception(std::current_exception());
        }
    };
};

namespace dawn_player::coroutine {
namespace impl {

struct as_coroutine {};

template<typename T>
std::future<T> task_to_future(as_coroutine, const task<T>& task)
{
	if constexpr (std::is_void_v<T>) {
		co_await task;
		co_return;
	} else {
		T result = co_await task;
		co_return result;
	}
}

} // namespace impl

template<typename T>
T sync_wait_task(const task<T>& task)
{
	return impl::task_to_future({}, task).get();
}

} // namespace dawn_player::coroutine

#endif
