/*
 *    io.hpp:
 *
 *    Copyright (C) 2015-2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_IO_HPP
#define DAWN_PLAYER_IO_HPP

#include <cstdint>

#include <winrt/Windows.Storage.Streams.h>

#include "coroutine/task.hpp"

using namespace winrt::Windows::Storage::Streams;

namespace dawn_player {
namespace io {

struct read_stream_proxy {
    virtual ~read_stream_proxy() {}
    virtual bool can_seek() const = 0;
    virtual coroutine::task<std::uint32_t> read(std::uint8_t* buf, std::uint32_t size) = 0;
    virtual void seek(std::uint64_t pos) = 0;
};

class ramdon_access_read_stream_proxy : public read_stream_proxy {
public:
    ramdon_access_read_stream_proxy(IRandomAccessStream stream);
    virtual ~ramdon_access_read_stream_proxy();
    virtual bool can_seek() const;
    virtual coroutine::task<std::uint32_t> read(std::uint8_t* buf, std::uint32_t size);
    virtual void seek(std::uint64_t pos);
private:
    IRandomAccessStream target;
};

class input_read_stream_proxy : public read_stream_proxy {
public:
    input_read_stream_proxy(IInputStream stream);
    virtual ~input_read_stream_proxy();
    virtual bool can_seek() const;
    virtual coroutine::task<std::uint32_t> read(std::uint8_t* buf, std::uint32_t size);
    virtual void seek(std::uint64_t pos);
private:
    IInputStream target;
};

} // namespace io
} // namespace dawn_player

#endif
