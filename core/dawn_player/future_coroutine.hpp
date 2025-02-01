/*
 *    future_coroutine.hpp:
 *
 *    Copyright (C) 2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FUTURE_COROUTINE_HPP
#define DAWN_PLAYER_FUTURE_COROUTINE_HPP

#include <coroutine>
#include <future>

// https://en.cppreference.com/w/cpp/coroutine/coroutine_traits
template<typename T, typename... Args>
    requires(!std::is_void_v<T> && !std::is_reference_v<T>)
struct std::coroutine_traits<std::future<T>, Args...>
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

template<typename... Args>
struct std::coroutine_traits<std::future<void>, Args...>
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

template<typename T>
auto operator co_await(std::future<T> future) noexcept
    requires(!std::is_reference_v<T>)
{
    struct awaiter : std::future<T>
    {
        bool await_ready() const noexcept
        {
            using namespace std::chrono_literals;
            return this->wait_for(0s) != std::future_status::timeout;
        }

        void await_suspend(std::coroutine_handle<> cont) const
        {
            std::thread([this, cont]
                {
                    this->wait();
                    cont();
                }).detach();
        }

        T await_resume() { return this->get(); }
    };

    return awaiter{ std::move(future) };
}

#endif
