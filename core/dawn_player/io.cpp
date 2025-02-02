/*
 *    io.cpp:
 *
 *    Copyright (C) 2015-2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include <stdexcept>

#include <ppltasks.h>
#include <robuffer.h>
#include <winrt/Windows.Foundation.h>

#include "io.hpp"

using namespace concurrency;
using namespace winrt::Windows::Foundation;

namespace dawn_player {
namespace io {

ramdon_access_read_stream_proxy::ramdon_access_read_stream_proxy(IRandomAccessStream stream)
    : target(stream)
{
}

ramdon_access_read_stream_proxy::~ramdon_access_read_stream_proxy()
{
    this->target = nullptr;
}

bool ramdon_access_read_stream_proxy::can_seek() const
{
    return true;
}

coroutine::task<std::uint32_t> ramdon_access_read_stream_proxy::read(std::uint8_t* buf, std::uint32_t size)
{
    auto buffer = co_await this->target.ReadAsync(Buffer(size), size, InputStreamOptions::Partial);
    std::uint32_t result = buffer.Length();
    if (result != 0) {
        auto raw_buffer = buffer.data();
        std::memcpy(buf, raw_buffer, result);
    }
    co_return result;
}

void ramdon_access_read_stream_proxy::seek(std::uint64_t pos)
{
    try {
        this->target.Seek(pos);
    }
    catch (...) {
        throw std::runtime_error("failed to seek");
    }
}

input_read_stream_proxy::input_read_stream_proxy(IInputStream stream)
    : target(stream)
{
}

input_read_stream_proxy::~input_read_stream_proxy()
{
    this->target = nullptr;
}

bool input_read_stream_proxy::can_seek() const
{
    return false;
}

coroutine::task<std::uint32_t> input_read_stream_proxy::read(std::uint8_t* buf, std::uint32_t size)
{
    auto buffer = co_await this->target.ReadAsync(Buffer(size), size, InputStreamOptions::Partial);
    std::uint32_t result = buffer.Length();
    if (result != 0) {
        auto raw_buffer = buffer.data();
        std::memcpy(buf, raw_buffer, result);
    }
    co_return result;
}

void input_read_stream_proxy::seek(std::uint64_t pos)
{
    throw std::runtime_error("bad operation");
}

} // namespace io
} // namespace dawn_player
