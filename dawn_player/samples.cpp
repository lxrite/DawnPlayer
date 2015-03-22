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

} // namespace dawn_player
} // namespace sample
