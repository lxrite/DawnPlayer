/*
 *    io.cpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include <stdexcept>

#include <robuffer.h>
#include <wrl.h>

#include "io.hpp"

using namespace Microsoft::WRL;

namespace dawn_player {
namespace io {

ramdon_access_read_stream_proxy::ramdon_access_read_stream_proxy(IRandomAccessStream^ stream)
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

task<std::uint32_t> ramdon_access_read_stream_proxy::read(std::uint8_t* buf, std::uint32_t size)
{
    auto tce = task_completion_event<std::uint32_t>();
    auto result_task = task<std::uint32_t>(tce);
    try {
        create_task(this->target->ReadAsync(ref new Buffer(size), size, InputStreamOptions::Partial))
            .then([tce, buf](task<IBuffer^> tsk) {
            IBuffer^ buffer = nullptr;
            try {
                buffer = tsk.get();
            }
            catch (...) {
                tce.set_exception(std::current_exception());
                return;
            }
            std::uint32_t size = buffer->Length;
            if (size != 0) {
                ComPtr<IBufferByteAccess> buffer_byte_access;
                reinterpret_cast<IInspectable*>(buffer)->QueryInterface(IID_PPV_ARGS(&buffer_byte_access));
                byte* raw_buffer = nullptr;
                buffer_byte_access->Buffer(&raw_buffer);
                std::memcpy(buf, raw_buffer, size);
            }
            tce.set(size);
        });
    }
    catch (...) {
        tce.set_exception(std::current_exception());
    }
    return result_task;
}

void ramdon_access_read_stream_proxy::seek(std::uint64_t pos)
{
    try {
        this->target->Seek(pos);
    }
    catch (...) {
        throw std::runtime_error("failed to seek");
    }
}

input_read_stream_proxy::input_read_stream_proxy(IInputStream^ stream)
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

task<std::uint32_t> input_read_stream_proxy::read(std::uint8_t* buf, std::uint32_t size)
{
    auto tce = task_completion_event<std::uint32_t>();
    auto result_task = task<std::uint32_t>(tce);
    try {
        create_task(this->target->ReadAsync(ref new Buffer(size), size, InputStreamOptions::Partial))
            .then([tce, buf](task<IBuffer^> tsk) {
            IBuffer^ buffer = nullptr;
            try {
                buffer = tsk.get();
            }
            catch (...) {
                tce.set_exception(std::current_exception());
                return;
            }
            std::uint32_t size = buffer->Length;
            if (size != 0) {
                ComPtr<IBufferByteAccess> buffer_byte_access;
                reinterpret_cast<IInspectable*>(buffer)->QueryInterface(IID_PPV_ARGS(&buffer_byte_access));
                byte* raw_buffer = nullptr;
                buffer_byte_access->Buffer(&raw_buffer);
                std::memcpy(buf, raw_buffer, size);
            }
            tce.set(size);
        });
    }
    catch (...) {
        tce.set_exception(std::current_exception());
    }
    return result_task;
}

void input_read_stream_proxy::seek(std::uint64_t pos)
{
    throw std::runtime_error("bad operation");
}

} // namespace io
} // namespace dawn_player
