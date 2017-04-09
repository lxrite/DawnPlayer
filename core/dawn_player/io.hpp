/*
 *    io.hpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_IO_HPP
#define DAWN_PLAYER_IO_HPP

#include <cstdint>

#include <ppltasks.h>

using namespace concurrency;
using namespace Windows::Storage::Streams;

namespace dawn_player {
namespace io {

struct read_stream_proxy {
    virtual ~read_stream_proxy() {}
    virtual bool can_seek() const = 0;
    virtual task<std::uint32_t> read(std::uint8_t* buf, std::uint32_t size) = 0;
    virtual void seek(std::uint64_t pos) = 0;
};

class ramdon_access_read_stream_proxy : public read_stream_proxy {
public:
    ramdon_access_read_stream_proxy(IRandomAccessStream^ stream);
    virtual ~ramdon_access_read_stream_proxy();
    virtual bool can_seek() const;
    virtual task<std::uint32_t> read(std::uint8_t* buf, std::uint32_t size);
    virtual void seek(std::uint64_t pos);
private:
    IRandomAccessStream^ target;
};

} // namespace io
} // namespace dawn_player

#endif
