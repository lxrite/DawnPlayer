/*
 *    flv_player.hpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FLV_PLAYER_HPP
#define DAWN_PLAYER_FLV_PLAYER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "amf_types.hpp"
#include "flv_parser.hpp"

using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

using namespace dawn_player::amf;
using namespace dawn_player::sample;

namespace dawn_player {

public enum class sample_type {
    audio,
    video
};

enum class open_result {
    ok,
    error,
    abort
};

public delegate void open_media_completed_handler(IMap<Platform::String^, Platform::String^>^ media_info);
public delegate void get_sample_completed_handler(sample_type type, IMap<Platform::String^, Platform::Object^>^ sample_info);

public ref class flv_player sealed {
private:
    IRandomAccessStream^ video_file_stream;
    IAsyncOperationWithProgress<IBuffer^, std::uint32_t>^ async_read_operation;
    std::vector<std::uint8_t> read_buffer;
    std::vector<std::uint8_t> swap_buffer;
    dawn_player::parser::flv_parser flv_parser;

    std::shared_ptr<dawn_player::amf::amf_ecma_array> flv_meta_data;
    bool is_video_cfg_read;
    bool is_audio_cfg_read;
    std::string video_codec_private_data;
    std::string audio_codec_private_data;

    std::atomic<std::uint32_t> pending_sample_cnt;

    std::mutex mtx;
    std::condition_variable sample_consumer_cv;
    std::condition_variable sample_producer_cv;
    std::deque<audio_sample> audio_sample_queue;
    std::deque<video_sample> video_sample_queue;

    std::thread parse_thread;
public:
    flv_player();
    void set_source(IRandomAccessStream^ random_access_stream);
    void open_async();
    void get_sample_async(sample_type type);
public:
    event open_media_completed_handler^ open_media_completed_event;
    event get_sample_completed_handler^ get_sample_competed_event;
private:
    open_result do_open();
    void do_get_sample();
private:
    bool on_script_tag(std::shared_ptr<dawn_player::amf::amf_base> name, std::shared_ptr<dawn_player::amf::amf_base> value);
    bool on_avc_decoder_configuration_record(const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps);
    bool on_audio_specific_config(const dawn_player::parser::audio_special_config& asc);
    bool on_audio_sample(audio_sample&& sample);
    bool on_video_sample(video_sample&& sample);

    void register_callback_functions();
    void unregister_callback_functions();

    void parse_thread_proc();

    std::string uint8_to_hex_string(const std::uint8_t* data, size_t size, bool uppercase = true) const;
};

} // namespace dawn_player

#endif
