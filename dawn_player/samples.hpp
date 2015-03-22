/*
 *    samples.hpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_SAMPLES_HPP
#define DAWN_PLAYER_SAMPLES_HPP

#include <cstdint>
#include <vector>

namespace dawn_player {
namespace sample {

struct audio_sample {
    std::int64_t timestamp;
    std::vector<unsigned char> data;
    audio_sample();
    audio_sample(const audio_sample& other);
    audio_sample(audio_sample&& other);
    audio_sample& operator=(const audio_sample& other);
    audio_sample& operator=(audio_sample&& other);
};

typedef audio_sample video_sample;

} //end of sample
} //end of dawn_player

#endif
