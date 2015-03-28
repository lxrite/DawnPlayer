/*
 *    flv_player.cpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "pch.h"

#include <collection.h>
#include <ppltasks.h>

#include "amf_decode.hpp"
#include "flv_player.hpp"

namespace dawn_player {

static const size_t BUFFER_QUEUE_DEFICIENT_SIZE = 100;
static const size_t BUFFER_QUEUE_SUFFICIENT_SIZE = 2000;

flv_player::flv_player() :
    is_video_cfg_read(false),
    is_audio_cfg_read(false),
    pending_sample_cnt(0)
{
}

void flv_player::set_source(IRandomAccessStream^ random_access_stream)
{
    this->video_file_stream = random_access_stream;
}

void flv_player::open_async()
{
    this->parse_thread = std::thread([this]() {
        this->parse_thread_proc();
    });
}

void flv_player::get_sample_async(sample_type type)
{
    std::uint32_t inc_value = (type == sample_type::audio ? 1 : 0x10000);
    if (this->pending_sample_cnt.fetch_add(inc_value, std::memory_order_acq_rel) == 0) {
        concurrency::create_async([this]() {
            this->do_get_sample();
        });
    }
}

open_result flv_player::do_open()
{
    IBuffer^ buffer = ref new Buffer(65536);
    this->async_read_operation = this->video_file_stream->ReadAsync(buffer, 65536, InputStreamOptions::None);
    auto async_read_task = concurrency::create_task(this->async_read_operation);
    buffer = async_read_task.get();
    if(buffer->Length < 13) {
        return open_result::error;
    }
    this->read_buffer.reserve(buffer->Length);
    auto data_reader = Windows::Storage::Streams::DataReader::FromBuffer(buffer);
    for (std::uint32_t i = 0; i != buffer->Length; ++i) {
        this->read_buffer.push_back(data_reader->ReadByte());
    }
    size_t size_consumed = 0;
    if (dawn_player::parser::parse_result::ok != this->flv_parser.parse_flv_header(this->read_buffer.data(), this->read_buffer.size(), size_consumed)) {
        return open_result::error;
    }
    this->register_callback_functions();
    this->flv_parser.parse_flv_tags(this->read_buffer.data() + 13, this->read_buffer.size() - 13, size_consumed);
    this->unregister_callback_functions();
    if (!(this->flv_meta_data && this->is_audio_cfg_read && this->is_video_cfg_read)) {
        return open_result::error;
    }
    std::memmove(this->read_buffer.data(), this->read_buffer.data() + 13 + size_consumed, this->read_buffer.size() - 13 - size_consumed);
    this->read_buffer.resize(this->read_buffer.size() - 13 - size_consumed);

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
    }
    auto media_info = ref new Platform::Collections::Map<Platform::String^, Platform::String^>();
    if (!duration) {
        return open_result::error;
    }
    media_info->Insert(L"Duration", ref new Platform::String(std::to_wstring(duration->get_value() * 10000000).c_str()));
    media_info->Insert(L"CanSeek", L"False");
    std::wstring audio_codec_private_data_w;
    std::transform(this->audio_codec_private_data.begin(), this->audio_codec_private_data.end(), std::back_inserter(audio_codec_private_data_w), [](char ch) -> wchar_t {
        return static_cast<wchar_t>(ch);
    });
    media_info->Insert(L"AudioCodecPrivateData", ref new Platform::String(audio_codec_private_data_w.c_str()));
    if (!width || !height) {
        return open_result::error;
    }
    media_info->Insert(L"Height", ref new Platform::String(std::to_wstring(height->get_value()).c_str()));
    media_info->Insert(L"Width", ref new Platform::String(std::to_wstring(width->get_value()).c_str()));

    // Only surpport AVC
    media_info->Insert(L"VideoFourCC", L"H264");
    std::wstring video_codec_private_data_w;
    std::transform(this->video_codec_private_data.begin(), this->video_codec_private_data.end(), std::back_inserter(video_codec_private_data_w), [](char ch) -> wchar_t {
        return static_cast<wchar_t>(ch);
    });
    media_info->Insert(L"VideoCodecPrivateData", ref new Platform::String(video_codec_private_data_w.c_str()));
    this->open_media_completed_event(media_info);
    return open_result::ok;
}

void flv_player::do_get_sample()
{
    std::uint32_t dec_value;
    do {
        audio_sample a_sample;
        video_sample v_sample;
        sample_type type;
        {
            std::unique_lock<std::mutex> lck(this->mtx);
            std::uint32_t pending_cnt = 0;
            if (this->audio_sample_queue.size() < BUFFER_QUEUE_DEFICIENT_SIZE ||
                this->video_sample_queue.size() < BUFFER_QUEUE_DEFICIENT_SIZE) {
                    this->sample_producer_cv.notify_one();
            }
            this->sample_consumer_cv.wait(lck, [this, &pending_cnt]() -> bool {
                pending_cnt = this->pending_sample_cnt.load(std::memory_order_acquire);
                if (this->audio_sample_queue.empty() && this->video_sample_queue.empty()) {
                    return false;
                }
                if ((pending_cnt & 0xffff) == 0 && this->video_sample_queue.empty()) {
                    return false;
                }
                if ((pending_cnt & 0xffff0000) == 0 && this->audio_sample_queue.empty()) {
                    return false;
                }
                return true;
            });
            if (this->audio_sample_queue.empty() || (pending_cnt & 0xffff) == 0) {
                type = sample_type::video;
            }
            else if (this->video_sample_queue.empty() || (pending_cnt & 0xffff0000) == 0) {
                type = sample_type::audio;
            }
            else {
                if (this->audio_sample_queue[0].timestamp < this->video_sample_queue[0].timestamp) {
                    type = sample_type::audio;
                }
                else {
                    type = sample_type::video;
                }
            }
            if (type == sample_type::audio) {
                a_sample = std::move(this->audio_sample_queue.front());
                this->audio_sample_queue.pop_front();
            }
            else {
                v_sample = std::move(this->video_sample_queue.front());
                this->video_sample_queue.pop_front();
            }
        }
        auto sample_info = ref new Platform::Collections::Map<Platform::String^, Platform::Object^>();
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
        }
        this->get_sample_competed_event(type, sample_info);
        dec_value = (type == sample_type::audio ? 1 : 0x10000);
    } while (this->pending_sample_cnt.fetch_sub(dec_value, std::memory_order_acq_rel) - dec_value != 0);
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
    this->sample_consumer_cv.notify_all();
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
    this->sample_consumer_cv.notify_all();
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

void flv_player::parse_thread_proc()
{
    if (this->do_open() == open_result::ok) {
        IBuffer^ buffer = ref new Buffer(65536);
        this->async_read_operation = this->video_file_stream->ReadAsync(buffer, 65536, InputStreamOptions::None);
        auto async_read_task = Concurrency::create_task(this->async_read_operation);
        buffer = async_read_task.get();
        this->register_callback_functions();
        for (; buffer->Length != 0;) {
            this->read_buffer.reserve(this->read_buffer.size() + buffer->Length);
            auto data_reader = Windows::Storage::Streams::DataReader::FromBuffer(buffer);
            for (std::uint32_t i = 0; i != buffer->Length; ++i) {
                this->read_buffer.push_back(data_reader->ReadByte());
            }
            this->async_read_operation = this->video_file_stream->ReadAsync(buffer, 65536, InputStreamOptions::None);
            auto async_read_task = Concurrency::create_task(this->async_read_operation);
            size_t bytes_consumed = 0;
            auto parse_result = this->flv_parser.parse_flv_tags(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
            if (parse_result != dawn_player::parser::parse_result::ok) {
                break;
            }
            std::memmove(this->read_buffer.data(), this->read_buffer.data() + bytes_consumed, this->read_buffer.size() - bytes_consumed);
            this->read_buffer.resize(this->read_buffer.size() - bytes_consumed);
            this->sample_consumer_cv.notify_one();
            {
                std::unique_lock<std::mutex> lck(this->mtx);
                this->sample_producer_cv.wait(lck, [this]() {
                    if (this->audio_sample_queue.size() < BUFFER_QUEUE_SUFFICIENT_SIZE || this->video_sample_queue.size() < BUFFER_QUEUE_SUFFICIENT_SIZE) {
                        return true;
                    }
                    return false;
                });
            }
            buffer = async_read_task.get();
        }
        this->unregister_callback_functions();
    }
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
