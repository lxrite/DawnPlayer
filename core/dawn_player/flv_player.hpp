/*
 *    flv_player.hpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FLV_PLAYER_HPP
#define DAWN_PLAYER_FLV_PLAYER_HPP

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <vector>

#include <ppltasks.h>

#include "amf_types.hpp"
#include "flv_parser.hpp"
#include "io.hpp"
#include "task_service.hpp"

using namespace Windows::Storage::Streams;
using namespace concurrency;
using namespace dawn_player::amf;
using namespace dawn_player::io;
using namespace dawn_player::sample;
using namespace dawn_player::parser;

namespace dawn_player {

class flv_player : public std::enable_shared_from_this<flv_player> {
    std::shared_ptr<task_service> tsk_service;
    std::shared_ptr<read_stream_proxy> stream_proxy;
    std::array<std::uint8_t, 65536> stream_proxy_read_buffer;
    std::vector<std::uint8_t> read_buffer;
    flv_parser parser;

    std::shared_ptr<amf_ecma_array> flv_meta_data;
    bool is_video_cfg_read;
    bool is_audio_cfg_read;
    std::string audio_codec_private_data;
    std::string video_codec_private_data;
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;

    std::int64_t position;
    bool is_closed;

    std::deque<audio_sample> audio_sample_queue;
    std::deque<video_sample> video_sample_queue;
    std::map<double, std::uint64_t, std::greater<double>> keyframes;

    std::queue<task_completion_event<std::int64_t>> get_start_position_tce_queue;
    std::deque<task_completion_event<audio_sample>> audio_sample_tce_queue;
    std::deque<task_completion_event<video_sample>> video_sample_tce_queue;
    std::queue<task_completion_event<std::int64_t>> seek_tce_queue;
    std::queue<task_completion_event<void>> close_tce_queue;

    bool is_end_of_stream;
    bool is_error_ocurred;
    bool is_sample_reading;
    std::int64_t last_seek_to_time;

    bool first_sample_timestamp_has_value;
    std::int64_t first_sample_timestamp;
    bool can_seek;

public:
    explicit flv_player(const std::shared_ptr<task_service>& task_service, const std::shared_ptr<read_stream_proxy>& stream_proxy);
    virtual ~flv_player();
    task<std::map<std::string, std::string>> open();
    task<audio_sample> get_audio_sample();
    task<video_sample> get_video_sample();
    task<std::int64_t> seek(std::int64_t seek_to_time);
    task<void> close();
    const std::vector<std::uint8_t>& get_sps() const;
    const std::vector<std::uint8_t>& get_pps() const;
    const std::shared_ptr<task_service> get_task_service() const;

private:
    task<std::uint32_t> read_some_data();
    task<void> parse_header();
    task<void> parse_meta_data();
    std::map<std::string, std::string> get_video_info();

private:
    void deliver_samples();
    void handle_seek();
    void handle_close();
    void read_more_sample();

private:
    bool on_script_tag(std::shared_ptr<amf_base> name, std::shared_ptr<amf_base> value);
    bool on_avc_decoder_configuration_record(const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps);
    bool on_audio_specific_config(const audio_special_config& asc);
    bool on_audio_sample(audio_sample&& sample);
    bool on_video_sample(video_sample&& sample);
    void register_callback_functions(bool sample_only);
    void unregister_callback_functions();
    std::string uint8_to_hex_string(const std::uint8_t* data, size_t size, bool uppercase = true) const;
    std::int64_t adjust_sample_timestamp(std::int64_t);
};

} // namespace dawn_player

#endif
