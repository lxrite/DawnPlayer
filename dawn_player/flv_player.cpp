/*
 *    flv_player.cpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "pch.h"

#include <future>
#include <memory>

#include <collection.h>
#include <ppltasks.h>

#include "amf_decode.hpp"
#include "flv_player.hpp"

using namespace dawn_player::parser;

namespace dawn_player {

static const size_t BUFFER_QUEUE_DEFICIENT_SIZE = 100;
static const size_t BUFFER_QUEUE_SUFFICIENT_SIZE = 500;

flv_player::flv_player() :
    is_video_cfg_read(false),
    is_audio_cfg_read(false),
    pending_sample_cnt(0),
    is_seek_pending(false),
    position(0),
    is_seeking(false),
    is_closing(false),
    is_sample_producer_working(false),
    is_all_sample_read(false),
    is_error_ocurred(false)
{
}

void flv_player::set_source(IRandomAccessStream^ random_access_stream)
{
    this->video_file_stream = random_access_stream;
}

IAsyncOperation<open_result>^ flv_player::open_async(IMap<Platform::String^, Platform::String^>^ media_info)
{
    auto promise = std::make_shared<std::promise<open_result>>();
    auto future = std::shared_future<open_result>(promise->get_future());
    this->is_sample_producer_working = true;
    this->sample_producer_thread = std::thread([this, media_info, promise]() {
        this->register_callback_functions();
        auto open_res = this->do_open(media_info);
        this->unregister_callback_functions();
        promise->set_value(open_res);
        if (open_res != open_result::ok) {
            return;
        }
        this->parse_flv_file_body();
    });
    return concurrency::create_async([this, future, media_info]() -> open_result {
        return future.get();
    });
}

IAsyncOperation<get_sample_result>^ flv_player::get_sample_async(sample_type type, IMap<Platform::String^, Platform::Object^>^ sample_info)
{
    this->pending_sample_cnt.fetch_add(1, std::memory_order_acq_rel);
    return concurrency::create_async([this, type, sample_info]() -> get_sample_result {
        get_sample_result result = this->do_get_sample(type, sample_info);
        this->pending_sample_cnt.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    });
}

IAsyncOperation<std::int64_t>^ flv_player::begin_seek(std::int64_t seek_to_time)
{
    if (this->keyframes.empty()) {
        assert(false);
    }
    if (this->is_seek_pending.exchange(true, std::memory_order_acq_rel)) {
        assert(false);
    }
    return concurrency::create_async([this, seek_to_time]() -> std::int64_t {
        std::int64_t _seek_to_time = seek_to_time;
        seek_result result = this->do_seek(_seek_to_time);
        this->is_seek_pending.store(false, std::memory_order_release);
        if (result == seek_result::ok) {
            this->position.store(_seek_to_time, std::memory_order_release);
            return _seek_to_time;
        }
        else {
            return -1;
        }
    });
}

void flv_player::end_seek()
{
    std::lock_guard<std::mutex> lck(this->mtx);
    this->is_seeking = false;
    this->sample_producer_cv.notify_one();
}

void flv_player::close()
{
    {
        std::lock_guard<std::mutex> lck(this->mtx);
        this->is_closing = true;
        if (this->async_read_operation) {
            this->async_read_operation->Cancel();
        }
    }
    this->sample_consumer_cv.notify_all();
    this->sample_producer_cv.notify_one();
    this->seeker_cv.notify_one();
    if (this->sample_producer_thread.joinable()) {
        this->sample_producer_thread.join();
    }
    while (this->pending_sample_cnt.load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
    while (this->is_seek_pending.load(std::memory_order_acquire) != false) {
        std::this_thread::yield();
    }

    // reset
    this->video_file_stream = nullptr;
    this->async_read_operation = nullptr;
    this->read_buffer.clear();
    this->flv_parser.reset();
    this->flv_meta_data.reset();
    this->is_video_cfg_read = false;
    this->is_audio_cfg_read = false;
    this->position.store(0, std::memory_order_release);
    this->audio_codec_private_data.clear();
    this->video_codec_private_data.clear();
    this->audio_sample_queue.clear();
    this->video_sample_queue.clear();
    this->keyframes.clear();
    this->is_seeking = false;
    this->is_closing = false;
    this->is_sample_producer_working = false;
    this->is_all_sample_read = false;
    this->is_error_ocurred = false;
}

std::int64_t flv_player::get_position()
{
    return this->position.load(std::memory_order_acquire);
}

open_result flv_player::do_open(IMap<Platform::String^, Platform::String^>^ media_info)
{
    IBuffer^ buffer = ref new Buffer(65536);
    {
        std::lock_guard<std::mutex> lck(this->mtx);
        if (this->is_closing) {
            return open_result::abort;
        }
        this->async_read_operation = this->video_file_stream->ReadAsync(buffer, this->flv_parser.first_tag_offset(), InputStreamOptions::None);
    }
    auto async_read_task = concurrency::create_task(this->async_read_operation);
    try {
        buffer = async_read_task.get();
    }
    catch (const concurrency::task_canceled&) {
    }
    {
        std::lock_guard<std::mutex> lck(this->mtx);
        this->async_read_operation = nullptr;
        if (this->is_closing) {
            return open_result::abort;
        }
    }
    if(buffer->Length != this->flv_parser.first_tag_offset()) {
        return open_result::error;
    }
    this->read_buffer.reserve(buffer->Length);
    auto data_reader = Windows::Storage::Streams::DataReader::FromBuffer(buffer);
    for (std::uint32_t i = 0; i != buffer->Length; ++i) {
        this->read_buffer.push_back(data_reader->ReadByte());
    }
    size_t size_consumed = 0;
    auto result = this->flv_parser.parse_flv_header(this->read_buffer.data(), this->read_buffer.size(), size_consumed);
    if (result != parse_result::ok) {
        return (result == parse_result::error ? open_result::error : open_result::abort);
    }
    this->read_buffer.clear();
    for (;;) {
        {
            std::lock_guard<std::mutex> lck(this->mtx);
            if (this->is_closing) {
                return open_result::abort;
            }
            this->async_read_operation = this->video_file_stream->ReadAsync(buffer, 65536, InputStreamOptions::None);
        }
        auto async_read_task = concurrency::create_task(this->async_read_operation);
        try {
            buffer = async_read_task.get();
        }
        catch (const concurrency::task_canceled&) {
            return open_result::abort;
        }
        {
            std::lock_guard<std::mutex> lck(this->mtx);
            this->async_read_operation = nullptr;
            if (this->is_closing) {
                return open_result::abort;
            }
        }
        if (buffer->Length == 0) {
            return open_result::error;
        }
        this->read_buffer.reserve(this->read_buffer.size() + buffer->Length);
        auto data_reader = Windows::Storage::Streams::DataReader::FromBuffer(buffer);
        for (std::uint32_t i = 0; i != buffer->Length; ++i) {
            this->read_buffer.push_back(data_reader->ReadByte());
        }
        result = this->flv_parser.parse_flv_tags(this->read_buffer.data(), this->read_buffer.size(), size_consumed);
        if (result != parse_result::ok) {
            return (result == parse_result::error ? open_result::error : open_result::abort);
        }
        if (size_consumed != 0) {
            std::memmove(this->read_buffer.data(), this->read_buffer.data() + size_consumed, this->read_buffer.size() - size_consumed);
            this->read_buffer.resize(this->read_buffer.size() - size_consumed);
        }
        if (this->flv_meta_data && this->is_audio_cfg_read && this->is_video_cfg_read) {
            break;
        }
    }

    // duration: a DOUBLE indicating the total duration of the file in seconds
    std::shared_ptr<amf_number> duration;
    // width: a DOUBLE indicating the width of the video in pixels
    std::shared_ptr<amf_number> width;
    // height: a DOUBLE indicating the height of the video in pixels
    std::shared_ptr<amf_number> height;
    // videodatarate: a DOUBLE indicating the video bit rate in kilobits per second
    std::shared_ptr<amf_number> video_data_rate;
    // framerate: a DOUBLE indicating the number of frames per second
    std::shared_ptr<amf_number> frame_rate;
    // videocodecid: a DOUBLE indicating the video codec ID used in the file
    std::shared_ptr<amf_number> video_codec_id;
    // audiosamplerate: a DOUBLE indicating the frequency at which the audio stream is replayed
    std::shared_ptr<amf_number> audio_sample_rate;
    // audiosamplesize: a DOUBLE indicating the resolution of a single audio sample
    std::shared_ptr<amf_number> audio_sample_size;
    // stereo: a BOOL indicating whether the data is stereo
    std::shared_ptr<amf_boolean> stereo;
    // audiocodecid: a DOUBLE indicating the audio codec ID used in the file
    std::shared_ptr<amf_number> audio_codec_id;
    // filesize: a DOUBLE indicating the total size of the file in bytes
    std::shared_ptr<amf_number> filesize;
    
    // extend
    // hasKeyframes
    std::shared_ptr<amf_boolean> has_keyframes;
    // hasVideo
    std::shared_ptr<amf_boolean> has_video;
    // hasAudio
    std::shared_ptr<amf_boolean> has_audio;
    // hasMetadata
    std::shared_ptr<amf_boolean> has_meta_data;
    // keyframes
    std::shared_ptr<amf_object> keyframes;

    auto iter = this->flv_meta_data->find("duration");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        duration = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("width");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        width = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("height");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        height = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("videodatarate");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        video_data_rate = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("framerate");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        frame_rate = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("videocodecid");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        video_codec_id = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("audiosamplerate");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        audio_sample_rate = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("audiosamplesize");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        audio_sample_size = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("stereo");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::boolean) {
        stereo = std::dynamic_pointer_cast<amf_boolean, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("audiocodecid");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        audio_codec_id = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("filesize");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::number) {
        filesize = std::dynamic_pointer_cast<amf_number, amf_base>(std::get<1>(*iter));
    }

    iter = this->flv_meta_data->find("hasKeyframes");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::boolean) {
        has_keyframes = std::dynamic_pointer_cast<amf_boolean, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("hasVideo");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::boolean) {
        has_video = std::dynamic_pointer_cast<amf_boolean, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("hasAudio");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::boolean) {
        has_audio = std::dynamic_pointer_cast<amf_boolean, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("hasMetadata");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::boolean) {
        has_meta_data = std::dynamic_pointer_cast<amf_boolean, amf_base>(std::get<1>(*iter));
    }
    iter = this->flv_meta_data->find("keyframes");
    if (iter != this->flv_meta_data->end() && std::get<1>(*iter)->get_type() == amf_type::object) {
        keyframes = std::dynamic_pointer_cast<amf_object, amf_base>(std::get<1>(*iter));
        try {
            auto file_positions = std::dynamic_pointer_cast<amf_strict_array, amf_base>(keyframes->get_attribute_value("filepositions"));
            auto times = std::dynamic_pointer_cast<amf_strict_array, amf_base>(keyframes->get_attribute_value("times"));
            if (file_positions == nullptr || times == nullptr || file_positions->size() != times->size() || file_positions->size() == 0) {
                return open_result::error;
            }
            std::transform(times->begin(), times->end(), file_positions->begin(), std::inserter(this->keyframes, this->keyframes.begin()),
                [](const std::shared_ptr<amf_base>& time, const std::shared_ptr<amf_base>& file_position) {
                    return std::make_pair(std::dynamic_pointer_cast<amf_number, amf_base>(time)->get_value(), static_cast<std::uint64_t>(std::dynamic_pointer_cast<amf_number,amf_base>(file_position)->get_value()));
                }
            );
        }
        catch (const std::bad_cast&) {
            return open_result::error;
        }
    }
    auto info = ref new Platform::Collections::Map<Platform::String^, Platform::String^>();
    if (!duration) {
        return open_result::error;
    }
    info->Insert(L"Duration", ref new Platform::String(std::to_wstring(duration->get_value() * 10000000).c_str()));
    if (!this->keyframes.empty()) {
        info->Insert(L"CanSeek", L"True");
    }
    else {
        info->Insert(L"CanSeek", L"False");
    }
    std::wstring audio_codec_private_data_w;
    std::transform(this->audio_codec_private_data.begin(), this->audio_codec_private_data.end(), std::back_inserter(audio_codec_private_data_w), [](char ch) -> wchar_t {
        return static_cast<wchar_t>(ch);
    });
    info->Insert(L"AudioCodecPrivateData", ref new Platform::String(audio_codec_private_data_w.c_str()));
    if (!width || !height) {
        return open_result::error;
    }
    info->Insert(L"Height", ref new Platform::String(std::to_wstring(height->get_value()).c_str()));
    info->Insert(L"Width", ref new Platform::String(std::to_wstring(width->get_value()).c_str()));

    // Only surpport AVC
    info->Insert(L"VideoFourCC", L"H264");
    std::wstring video_codec_private_data_w;
    std::transform(this->video_codec_private_data.begin(), this->video_codec_private_data.end(), std::back_inserter(video_codec_private_data_w), [](char ch) -> wchar_t {
        return static_cast<wchar_t>(ch);
    });
    info->Insert(L"VideoCodecPrivateData", ref new Platform::String(video_codec_private_data_w.c_str()));
    for (auto iter = info->First(); iter->HasCurrent; iter->MoveNext()) {
        media_info->Insert(iter->Current->Key, iter->Current->Value);
    }
    return open_result::ok;
}

get_sample_result flv_player::do_get_sample(sample_type type, IMap<Platform::String^, Platform::Object^>^ sample_info)
{
    audio_sample a_sample;
    video_sample v_sample;
    {
        std::unique_lock<std::mutex> lck (this->mtx);
        if (!this->is_all_sample_read && !this->is_error_ocurred &&
            (this->audio_sample_queue.size() < BUFFER_QUEUE_DEFICIENT_SIZE ||
            this->video_sample_queue.size() < BUFFER_QUEUE_DEFICIENT_SIZE)) {
                this->sample_producer_cv.notify_one();
        }
        if (type == sample_type::audio) {
            this->sample_consumer_cv.wait(lck, [this]() -> bool {
                if (this->is_closing) {
                    return true;
                }
                if (this->is_seeking) {
                    return false;
                }
                if (this->is_all_sample_read || this->is_error_ocurred) {
                    return true;
                }
                return !this->audio_sample_queue.empty();
            });
            if (this->is_closing) {
                return get_sample_result::abort;
            }
            if (this->audio_sample_queue.empty()) {
                if (this->is_all_sample_read) {
                    return get_sample_result::eos;
                }
                else {
                    return get_sample_result::error;
                }
            }
            else {
                a_sample = std::move(this->audio_sample_queue.front());
                this->audio_sample_queue.pop_front();
            }
        }
        else {
            this->sample_consumer_cv.wait(lck, [this]() -> bool {
                if (this->is_closing) {
                    return true;
                }
                if (this->is_seeking) {
                    return false;
                }
                if (this->is_all_sample_read || this->is_error_ocurred) {
                    return true;
                }
                return !this->video_sample_queue.empty();
            });
            if (this->is_closing) {
                return get_sample_result::abort;
            }
            if (this->video_sample_queue.empty()) {
                if (this->is_all_sample_read) {
                    return get_sample_result::eos;
                }
                else {
                    return get_sample_result::error;
                }
            }
            else {
                v_sample = std::move(this->video_sample_queue.front());
                this->video_sample_queue.pop_front();
            }
        }
    }
    if (type == sample_type::audio) {
        sample_info->Insert(L"Timestamp",ref new Platform::String(std::to_wstring(a_sample.timestamp).c_str()));
        auto data_writer = ref new DataWriter();
        for(auto byte : a_sample.data) {
            data_writer->WriteByte(byte);
        }
        IBuffer^ sample_data_buffer = data_writer->DetachBuffer();
        sample_info->Insert(L"Data", sample_data_buffer);
    }
    else {
        sample_info->Insert(L"Timestamp",ref new Platform::String(std::to_wstring(v_sample.timestamp).c_str()));
        auto data_writer = ref new DataWriter();
        for(auto byte : v_sample.data) {
            data_writer->WriteByte(byte);
        }
        IBuffer^ sample_data_buffer = data_writer->DetachBuffer();
        sample_info->Insert(L"Data", sample_data_buffer);
        this->position.store(v_sample.dts);
    }
    return get_sample_result::ok;
}

seek_result flv_player::do_seek(std::int64_t& seek_to_time)
{
    double seek_to_time_sec = static_cast<double>(seek_to_time) / 10000000;
    auto iter = this->keyframes.lower_bound(seek_to_time_sec);
    std::uint64_t position = 0;
    double time = 0.00;
    if (iter == this->keyframes.end()) {
        position = keyframes.rbegin()->second;
        time = keyframes.rbegin()->first;
    }
    else {
        position = iter->second;
        time = iter->first;
    }
    {
        std::unique_lock<std::mutex> lck(this->mtx);
        this->is_seeking = true;
        if (this->async_read_operation != nullptr) {
            this->async_read_operation->Cancel();
        }
        this->sample_producer_cv.notify_one();
        this->seeker_cv.wait(lck, [this]() -> bool {
            return this->is_closing || !this->is_sample_producer_working;
        });
        if (this->is_closing) {
            return seek_result::abort;
        }
        this->read_buffer.clear();
        this->audio_sample_queue.clear();
        this->video_sample_queue.clear();
        this->is_error_ocurred = false;
        this->is_all_sample_read = false;
        this->video_file_stream->Seek(position);
    }
    seek_to_time = static_cast<std::int64_t>(time * 10000000);
    return seek_result::ok;
}

bool flv_player::on_script_tag(std::shared_ptr<dawn_player::amf::amf_base> name, std::shared_ptr<dawn_player::amf::amf_base> value)
{
    using namespace dawn_player::amf;
    if (name->get_type() != amf_type::string || std::dynamic_pointer_cast<amf_string, amf_base>(name)->get_value() != "onMetaData") {
        return true;
    }
    if (value->get_type() != amf_type::ecma_array) {
        return false;
    }
    this->flv_meta_data = std::dynamic_pointer_cast<amf_ecma_array, amf_base>(value);
    return true;
}

bool flv_player::on_avc_decoder_configuration_record(const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps)
{
    this->video_codec_private_data;
    std::uint8_t prefix[3] = { 0x00, 0x00, 0x01 };
    this->video_codec_private_data += this->uint8_to_hex_string(prefix, sizeof(prefix));
    this->video_codec_private_data += this->uint8_to_hex_string(sps.data(), sps.size());
    this->video_codec_private_data += this->uint8_to_hex_string(prefix, sizeof(prefix));
    this->video_codec_private_data += this->uint8_to_hex_string(pps.data(), pps.size());
    this->is_video_cfg_read = true;
    return true;
}

bool flv_player::on_audio_specific_config(const dawn_player::parser::audio_special_config& asc)
{
    this->audio_codec_private_data;
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.format_tag), sizeof(asc.format_tag));
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.channels), sizeof(asc.channels));
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.sample_per_second), sizeof(asc.sample_per_second));
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.average_bytes_per_second), sizeof(asc.average_bytes_per_second));
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.block_align), sizeof(asc.block_align));
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.bits_per_sample), sizeof(asc.bits_per_sample));
    this->audio_codec_private_data += this->uint8_to_hex_string(reinterpret_cast<const std::uint8_t*>(&asc.size), sizeof(asc.size));
    this->is_audio_cfg_read = true;
    return true;
}

bool flv_player::on_audio_sample(audio_sample&& sample)
{
    if (!this->is_audio_cfg_read) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lck(this->mtx);
        this->audio_sample_queue.emplace_back(std::move(sample));
    }
    return true;
}

bool flv_player::on_video_sample(video_sample&& sample)
{
    if (!this->is_video_cfg_read) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lck(this->mtx);
        this->video_sample_queue.emplace_back(std::move(sample));
    }
    return true;
}

void flv_player::register_callback_functions()
{
    this->flv_parser.on_script_tag = [this](std::shared_ptr<dawn_player::amf::amf_base> name, std::shared_ptr<dawn_player::amf::amf_base> value) -> bool {
        return this->on_script_tag(name, value);
    };
    this->flv_parser.on_avc_decoder_configuration_record = [this](const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps) -> bool {
        return this->on_avc_decoder_configuration_record(sps, pps);
    };
    this->flv_parser.on_audio_specific_config = [this](const dawn_player::parser::audio_special_config& asc) -> bool {
        return this->on_audio_specific_config(asc);
    };
    this->flv_parser.on_audio_sample = [this](audio_sample&& sample) -> bool {
        return this->on_audio_sample(std::move(sample));
    };
    this->flv_parser.on_video_sample = [this](video_sample&& sample) -> bool {
        return this->on_video_sample(std::move(sample));
    };
}

void flv_player::unregister_callback_functions()
{
    this->flv_parser.on_script_tag = nullptr;
    this->flv_parser.on_avc_decoder_configuration_record = nullptr;
    this->flv_parser.on_audio_specific_config = nullptr;
    this->flv_parser.on_audio_sample = nullptr;
    this->flv_parser.on_video_sample = nullptr;
}

void flv_player::parse_flv_file_body()
{
    this->register_callback_functions();
    IBuffer^ buffer = ref new Buffer(65536);
    for (;;) {
        concurrency::task<IBuffer^> async_read_task;
        {
            std::lock_guard<std::mutex> lck(this->mtx);
            if (this->is_closing) {
                break;
            }
            this->async_read_operation = this->video_file_stream->ReadAsync(buffer, 65536, InputStreamOptions::None);
            async_read_task = Concurrency::create_task(this->async_read_operation);
        }
        try {
            buffer = async_read_task.get();
        }
        catch (const concurrency::task_canceled&) {
        }
        {
            std::unique_lock<std::mutex> lck(this->mtx);
            this->async_read_operation = nullptr;
            if (this->is_closing) {
                break;
            }
            else if (this->is_seeking) {
                this->is_sample_producer_working = false;
                this->seeker_cv.notify_one();
                this->sample_producer_cv.wait(lck, [this]() -> bool {
                    if (this->is_closing || !this->is_seeking) {
                        return true;
                    }
                    return false;
                });
                if (this->is_closing) {
                    break;
                }
                this->is_sample_producer_working = true;
                continue;
            }
        }
        parse_result parse_res = parse_result::ok;
        if (buffer->Length != 0) {
            this->read_buffer.reserve(this->read_buffer.size() + buffer->Length);
            auto data_reader = Windows::Storage::Streams::DataReader::FromBuffer(buffer);
            for (std::uint32_t i = 0; i != buffer->Length; ++i) {
                this->read_buffer.push_back(data_reader->ReadByte());
            }
            size_t bytes_consumed = 0;
            parse_res = this->flv_parser.parse_flv_tags(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
            if (parse_res == parse_result::ok) {
                std::memmove(this->read_buffer.data(), this->read_buffer.data() + bytes_consumed, this->read_buffer.size() - bytes_consumed);
                this->read_buffer.resize(this->read_buffer.size() - bytes_consumed);
            }
        }
        {
            std::unique_lock<std::mutex> lck(this->mtx);
            if (buffer->Length == 0) {
                this->is_all_sample_read = true;
            }
            else if (parse_res != parse_result::ok) {
                this->is_error_ocurred = true;
            }
            this->is_sample_producer_working = false;
            this->sample_consumer_cv.notify_all();
            this->seeker_cv.notify_one();
            this->sample_producer_cv.wait(lck, [this]() -> bool {
                if (this->is_closing) {
                    return true;
                }
                if (this->is_seeking) {
                    return false;
                }
                if (this->is_all_sample_read || this->is_error_ocurred) {
                    return false;
                }
                if (this->audio_sample_queue.size() < BUFFER_QUEUE_SUFFICIENT_SIZE || this->video_sample_queue.size() < BUFFER_QUEUE_SUFFICIENT_SIZE) {
                    return true;
                }
                return false;
            });
            if (this->is_closing) {
                break;
            }
            this->is_sample_producer_working = true;
        }
    }
    this->unregister_callback_functions();
}

std::string flv_player::uint8_to_hex_string(const std::uint8_t* data, size_t size, bool uppercase) const
{
    std::string result;
    char a_value = uppercase ? 'A' : 'a';
    for (size_t i = 0; i < size; ++i) {
        auto byte_value = static_cast<std::uint32_t>(data[i]);
        if ((byte_value >> 4) >= 10) {
            result.push_back(a_value + (byte_value >> 4) - 10);
        }
        else {
            result.push_back('0' + (byte_value >> 4));
        }
        if ((byte_value & 0x0f) >= 10) {
            result.push_back(a_value + ((byte_value & 0x0f)) - 10);
        }
        else {
            result.push_back('0' + ((byte_value & 0x0f)));
        }
    }
    return result;
}

} // namespace dawn_player
