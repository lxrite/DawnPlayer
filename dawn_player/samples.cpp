/*
 *    samples.cpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "pch.h"
#include "samples.hpp"

namespace dawn_player {
namespace sample {

audio_sample::audio_sample()
    : timestamp(0)
{
}

audio_sample::audio_sample(const audio_sample& other)
    : timestamp(other.timestamp), data(other.data)
{
}

audio_sample::audio_sample(audio_sample&& other)
    : timestamp(other.timestamp), data(std::move(other.data))
{
    other.timestamp = 0;
}

audio_sample& audio_sample::operator=(const audio_sample& other)
{
    this->timestamp = other.timestamp;
    this->data = other.data;
    return *this;
}

audio_sample& audio_sample::operator=(audio_sample&& other)
{
    this->timestamp = other.timestamp;
    this->data = std::move(other.data);
    other.timestamp = 0;
    return *this;
}

video_sample::video_sample()
    : dts(0), timestamp(0)
{
}

video_sample::video_sample(const video_sample& other)
    : dts(other.dts), timestamp(other.timestamp), data(other.data)
{
}

video_sample::video_sample(video_sample&& other)
    : dts(other.dts), timestamp(other.timestamp), data(std::move(other.data))
{
    other.dts = 0;
    other.timestamp = 0;
}

video_sample& video_sample::operator=(const video_sample& other)
{
    this->dts = other.dts;
    this->timestamp = other.timestamp;
    return *this;
}

video_sample& video_sample::operator=(video_sample&& other)
{
    this->dts = other.dts;
    this->timestamp = other.timestamp;
    this->data = std::move(other.data);
    other.dts = 0;
    other.timestamp = 0;
    return *this;
}

} // namespace sample
} // namespace dawn_player
