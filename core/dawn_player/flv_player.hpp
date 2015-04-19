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
#include <functional>
#include <map>
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

public enum class open_result {
    ok,
    error,
    abort
};

public enum class get_sample_result {
    ok,
    eos,
    error,
    abort
};

enum class seek_result {
    ok,
    ignore,
    abort
};

public ref class flv_player sealed {
private:
    IRandomAccessStream^ video_file_stream;
    IAsyncOperationWithProgress<IBuffer^, std::uint32_t>^ async_read_operation;
    std::vector<std::uint8_t> read_buffer;
    dawn_player::parser::flv_parser flv_parser;

    std::shared_ptr<dawn_player::amf::amf_ecma_array> flv_meta_data;
    bool is_video_cfg_read;
    bool is_audio_cfg_read;
    std::string audio_codec_private_data;
    std::string video_codec_private_data;
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;

    std::atomic<std::uint32_t> pending_sample_cnt;
    std::atomic<bool> is_seek_pending;
    std::atomic<std::int64_t> position;

    std::mutex mtx;
    std::condition_variable sample_consumer_cv;
    std::condition_variable sample_producer_cv;
    std::condition_variable seeker_cv;
    std::deque<audio_sample> audio_sample_queue;
    std::deque<video_sample> video_sample_queue;
    std::map<double, std::uint64_t, std::greater<double>> keyframes;

    std::thread sample_producer_thread;

    bool is_seeking;
    bool is_closing;
    bool is_closed;

    bool is_sample_producer_working;
    bool is_all_sample_read;
    bool is_error_ocurred;
public:
    flv_player();
    virtual ~flv_player();
    void set_source(IRandomAccessStream^ random_access_stream);
    IAsyncOperation<open_result>^ open_async(IMap<Platform::String^, Platform::String^>^ media_info);
    IAsyncOperation<get_sample_result>^ get_sample_async(sample_type type, IMap<Platform::String^, Platform::Object^>^ sample_info);
    IAsyncOperation<std::int64_t>^ begin_seek(std::int64_t seek_to_time);
    void end_seek();
    void close();

    std::int64_t get_position();
private:
    open_result do_open(IMap<Platform::String^, Platform::String^>^ media_info);
    get_sample_result do_get_sample(sample_type type, IMap<Platform::String^, Platform::Object^>^ sample_info);
    seek_result do_seek(std::int64_t& seek_to_time);
private:
    bool on_script_tag(std::shared_ptr<dawn_player::amf::amf_base> name, std::shared_ptr<dawn_player::amf::amf_base> value);
    bool on_avc_decoder_configuration_record(const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps);
    bool on_audio_specific_config(const dawn_player::parser::audio_special_config& asc);
    bool on_audio_sample(audio_sample&& sample);
    bool on_video_sample(video_sample&& sample);

    void register_callback_functions(bool sample_only);
    void unregister_callback_functions();

    void parse_flv_file_body();
    std::string uint8_to_hex_string(const std::uint8_t* data, size_t size, bool uppercase = true) const;
};

} // namespace dawn_player

#endif
