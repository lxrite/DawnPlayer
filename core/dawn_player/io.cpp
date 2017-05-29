/*
 *    io.cpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include <stdexcept>

#include <ppltasks.h>
#include <robuffer.h>
#include <wrl.h>

#include "io.hpp"

using namespace concurrency;
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

std::future<std::uint32_t> ramdon_access_read_stream_proxy::read(std::uint8_t* buf, std::uint32_t size)
{
    auto p = std::make_shared<std::promise<std::uint32_t>>();
    try {
        create_task(this->target->ReadAsync(ref new Buffer(size), size, InputStreamOptions::Partial))
            .then([p, buf](task<IBuffer^> tsk) {
            IBuffer^ buffer = nullptr;
            try {
                buffer = tsk.get();
            }
            catch (...) {
                p->set_exception(std::current_exception());
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
            p->set_value(size);
        });
    }
    catch (...) {
        p->set_exception(std::current_exception());
    }
    return p->get_future();
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

std::future<std::uint32_t> input_read_stream_proxy::read(std::uint8_t* buf, std::uint32_t size)
{
    auto p = std::make_shared<std::promise<std::uint32_t>>();
    try {
        create_task(this->target->ReadAsync(ref new Buffer(size), size, InputStreamOptions::Partial))
            .then([p, buf](task<IBuffer^> tsk) {
            IBuffer^ buffer = nullptr;
            try {
                buffer = tsk.get();
            }
            catch (...) {
                p->set_exception(std::current_exception());
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
            p->set_value(size);
        });
    }
    catch (...) {
        p->set_exception(std::current_exception());
    }
    return p->get_future();
}

void input_read_stream_proxy::seek(std::uint64_t pos)
{
    throw std::runtime_error("bad operation");
}

} // namespace io
} // namespace dawn_player
