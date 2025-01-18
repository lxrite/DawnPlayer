/*
 *    flv_player.cpp:
 *
 *    Copyright (C) 2015-2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "amf_decode.hpp"
#include "error.hpp"
#include "flv_player.hpp"

namespace dawn_player {

flv_player::flv_player(const std::shared_ptr<task_service>& tsk_service, const std::shared_ptr<read_stream_proxy>& stream_proxy)
    : tsk_service(tsk_service)
    , stream_proxy(stream_proxy)
    , is_video_cfg_read(false)
    , is_audio_cfg_read(false)
    , is_end_of_stream(false)
    , is_error_ocurred(false)
    , is_sample_reading(false)
    , is_closed(false)
    , first_sample_timestamp_has_value(false)
    , first_sample_timestamp(0)
    , can_seek(false)
{
}

flv_player::~flv_player()
{
}

std::future<std::map<std::string, std::string>> flv_player::open()
{
    co_await switch_to_task_service(this->tsk_service.get());
    co_await this->parse_header();
    co_await switch_to_task_service(this->tsk_service.get());
    co_await this->parse_meta_data();
    co_await switch_to_task_service(this->tsk_service.get());
    co_return this->get_video_info();
}

std::future<audio_sample> flv_player::get_audio_sample()
{
    co_await switch_to_task_service(this->tsk_service.get());
    for (;;) {
        if (this->is_closed) {
            throw get_sample_error("operation canceled", get_sample_error_code::cancel);
        }
        if (!this->audio_sample_queue.empty()) {
            break;
        }
        if (this->is_end_of_stream) {
            throw get_sample_error("end of stream", get_sample_error_code::end_of_stream);
        }
        if (this->is_error_ocurred) {
            throw get_sample_error("errror ocurred", get_sample_error_code::other);
        }
        if (this->is_sample_reading) {
            auto p = std::make_shared<std::promise<void>>();
            this->read_sample_promise_queue.push(p);
            co_await p->get_future();
        }
        else {
            co_await this->read_more_sample();
        }
        co_await switch_to_task_service(this->tsk_service.get());
    }
    auto sample = std::move(this->audio_sample_queue.front());
    this->audio_sample_queue.pop_front();
    co_return sample;
}

std::future<video_sample> flv_player::get_video_sample()
{
    co_await switch_to_task_service(this->tsk_service.get());
    for (;;) {
        if (this->is_closed) {
            throw get_sample_error("operation canceled", get_sample_error_code::cancel);
        }
        if (!this->video_sample_queue.empty()) {
            break;
        }
        if (this->is_end_of_stream) {
            throw get_sample_error("end of stream", get_sample_error_code::end_of_stream);
        }
        if (this->is_error_ocurred) {
            throw get_sample_error("errror ocurred", get_sample_error_code::other);
        }
        if (this->is_sample_reading) {
            auto p = std::make_shared<std::promise<void>>();
            this->read_sample_promise_queue.push(p);
            co_await p->get_future();
        }
        else {
            co_await this->read_more_sample();
        }
        co_await switch_to_task_service(this->tsk_service.get());
    }
    auto sample = std::move(this->video_sample_queue.front());
    this->video_sample_queue.pop_front();
    co_return sample;
}

std::future<std::int64_t> flv_player::seek(std::int64_t seek_to_time)
{
    co_await switch_to_task_service(this->tsk_service.get());
    for (;;) {
        if (this->is_closed) {
            throw seek_error("operation canceled", seek_error_code::cancel);
        }
        if (!this->is_sample_reading) {
            break;
        }
        auto p = std::make_shared<std::promise<void>>();
        this->read_sample_promise_queue.push(p);
        co_await p->get_future();
        co_await switch_to_task_service(this->tsk_service.get());
    }
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
    this->read_buffer.clear();
    this->audio_sample_queue.clear();
    this->video_sample_queue.clear();
    this->is_error_ocurred = false;
    this->is_end_of_stream = false;
    try {
        this->stream_proxy->seek(position);
    }
    catch (...) {
        this->is_error_ocurred = true;
    }
    co_return static_cast<std::int64_t>(time * 10000000);
}

std::future<void> flv_player::close()
{
    co_await switch_to_task_service(this->tsk_service.get());
    this->is_closed = true;
}

const std::vector<std::uint8_t>& flv_player::get_vps() const
{
    return this->vps;
}

const std::vector<std::uint8_t>& flv_player::get_sps() const
{
    return this->sps;
}

const std::vector<std::uint8_t>& flv_player::get_pps() const
{
    return this->pps;
}

const std::shared_ptr<task_service> flv_player::get_task_service() const
{
    return this->tsk_service;
}

video_codec flv_player::get_video_codec() const
{
    return this->video_codec_;
}

std::future<std::uint32_t> flv_player::read_some_data()
{
    co_await switch_to_task_service(this->tsk_service.get());
    const size_t buf_size = 65536;
    std::uint8_t buf[buf_size];
    auto size = co_await this->stream_proxy->read(buf, buf_size);
    co_await switch_to_task_service(tsk_service.get());
    if (size != 0) {
        this->read_buffer.reserve(this->read_buffer.size() + size);
        std::copy(buf, buf + size, std::back_inserter(this->read_buffer));
    }
    co_return size;
}

std::future<void> flv_player::parse_header()
{
    while (this->read_buffer.size() < this->parser.first_tag_offset()) {
        std::uint32_t size = 0;
        try {
            size = co_await this->read_some_data();
        }
        catch (...) {
            throw open_error("Read data error.", open_error_code::io_error);
        }
        if (size == 0) {
            throw open_error("Failed to parse header, end of stream.", open_error_code::parse_error);
        }
        co_await switch_to_task_service(this->tsk_service.get());
    }
    size_t bytes_consumed = 0;
    auto parse_res = this->parser.parse_flv_header(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
    if (parse_res != parse_result::ok) {
        throw open_error("Bad FLV header.", open_error_code::parse_error);
    }
    std::memmove(this->read_buffer.data(), this->read_buffer.data() + this->parser.first_tag_offset(), this->read_buffer.size() - this->parser.first_tag_offset());
    this->read_buffer.resize(this->read_buffer.size() - this->parser.first_tag_offset());
}

std::future<void> flv_player::parse_meta_data()
{
    for (;;) {
        std::uint32_t size = 0;
        try {
            size = co_await this->read_some_data();
        }
        catch (...) {
            throw open_error("Read data error.", open_error_code::io_error);
        }
        co_await switch_to_task_service(this->tsk_service.get());
        size_t bytes_consumed = 0;
        this->register_callback_functions(false);
        auto parse_res = this->parser.parse_flv_tags(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
        this->unregister_callback_functions();
        if (parse_res != parse_result::ok) {
            throw open_error("Bad FLV data.", open_error_code::parse_error);
        }
        std::memmove(this->read_buffer.data(), this->read_buffer.data() + bytes_consumed, this->read_buffer.size() - bytes_consumed);
        this->read_buffer.resize(this->read_buffer.size() - bytes_consumed);
        if (this->flv_meta_data && this->is_audio_cfg_read && this->is_video_cfg_read) {
            break;
        }
        if (size == 0) {
            throw open_error("Failed to parse header, end of stream.", open_error_code::parse_error);
        }
    }
}

std::map<std::string, std::string> flv_player::get_video_info()
{
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
                throw open_error("Bad keyframes data", open_error_code::parse_error);
            }
            std::transform(times->begin(), times->end(), file_positions->begin(), std::inserter(this->keyframes, this->keyframes.begin()),
                [](const std::shared_ptr<amf_base>& time, const std::shared_ptr<amf_base>& file_position) {
                return std::make_pair(std::dynamic_pointer_cast<amf_number, amf_base>(time)->get_value(), static_cast<std::uint64_t>(std::dynamic_pointer_cast<amf_number, amf_base>(file_position)->get_value()));
            });
        }
        catch (const std::bad_cast&) {
            throw open_error("Bad keyframes data", open_error_code::parse_error);
        }
    }

    auto info = std::map<std::string, std::string>();
    if (duration) {
        info["Duration"] = std::to_string(duration->get_value() * 10000000);
    }

    this->can_seek = this->stream_proxy->can_seek() && !this->keyframes.empty();
    if (this->can_seek) {
        info["CanSeek"] = std::string("True");
    }
    else {
        info["CanSeek"] = std::string("False");
    }
    info["AudioCodecPrivateData"] = audio_codec_private_data;
    if (!width || !height) {
        throw open_error("Miss width or height", open_error_code::parse_error);
    }
    info["Height"] = std::to_string(height->get_value());
    info["Width"] = std::to_string(width->get_value());
    return info;
}

std::future<void> flv_player::read_more_sample()
{
    co_await switch_to_task_service(this->tsk_service.get());
    assert(!this->is_sample_reading);
    this->is_sample_reading = true;
    bool err = false;
    std::uint32_t size = 0;
    try {
        size = co_await this->read_some_data();
    }
    catch (...) {
        err = true;
    }
    co_await switch_to_task_service(this->tsk_service.get());
    if (err) {
        this->is_error_ocurred = true;
    }
    else {
        if (size == 0) {
            this->is_end_of_stream = true;
        }
        else {
            size_t bytes_consumed = 0;
            this->register_callback_functions(true);
            auto parse_res = this->parser.parse_flv_tags(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
            this->unregister_callback_functions();
            if (parse_res != parse_result::ok) {
                this->is_error_ocurred = true;
            }
            else {
                std::memmove(this->read_buffer.data(), this->read_buffer.data() + bytes_consumed, this->read_buffer.size() - bytes_consumed);
                this->read_buffer.resize(this->read_buffer.size() - bytes_consumed);
            }
        }
    }
    this->is_sample_reading = false;
    while (!this->read_sample_promise_queue.empty()) {
        auto p = this->read_sample_promise_queue.front();
        this->read_sample_promise_queue.pop();
        p->set_value();
    }
}

bool flv_player::on_script_tag(std::shared_ptr<amf_base> name, std::shared_ptr<amf_base> value)
{
    if (this->flv_meta_data) {
        return true;
    }
    if (name->get_type() != amf_type::string || std::dynamic_pointer_cast<amf_string, amf_base>(name)->get_value() != "onMetaData") {
        return true;
    }
    if (value->get_type() == amf_type::ecma_array) {
        this->flv_meta_data = std::dynamic_pointer_cast<amf_ecma_array, amf_base>(value);
    }
    else if (value->get_type() == amf_type::object) {
        this->flv_meta_data = std::dynamic_pointer_cast<amf_object, amf_base>(value)->to_ecma_array();
    }
    else {
        return false;
    }
    return true;
}

bool flv_player::on_avc_decoder_configuration_record(const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps)
{
    this->video_codec_ = video_codec::h264;
    this->sps = sps;
    this->pps = pps;
    this->is_video_cfg_read = true;
    return true;
}

bool flv_player::on_hevc_decoder_configuration_record(const std::vector<std::uint8_t>& vps, const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps)
{
    this->video_codec_ = video_codec::hevc;
    this->vps = vps;
    this->sps = sps;
    this->pps = pps;
    this->is_video_cfg_read = true;
    return true;
}

bool flv_player::on_audio_specific_config(const audio_special_config& asc)
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
    if (!this->first_sample_timestamp_has_value) {
        this->first_sample_timestamp_has_value = true;
        this->first_sample_timestamp = sample.timestamp;
    }
    this->audio_sample_queue.emplace_back(std::move(sample));
    return true;
}

bool flv_player::on_video_sample(video_sample&& sample)
{
    if (!this->is_video_cfg_read) {
        return false;
    }
    if (!this->first_sample_timestamp_has_value) {
        this->first_sample_timestamp_has_value = true;
        this->first_sample_timestamp = sample.timestamp;
    }
    this->video_sample_queue.emplace_back(std::move(sample));
    return true;
}

void flv_player::register_callback_functions(bool sample_only)
{
    if (sample_only) {
        this->parser.on_script_tag = nullptr;
        this->parser.on_avc_decoder_configuration_record = nullptr;
        this->parser.on_hevc_decoder_configuration_record = nullptr;
        this->parser.on_audio_specific_config = nullptr;
    }
    else {
        this->parser.on_script_tag = [this](std::shared_ptr<amf_base> name, std::shared_ptr<amf_base> value) -> bool {
            return this->on_script_tag(name, value);
        };
        this->parser.on_avc_decoder_configuration_record = [this](const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps) -> bool {
            return this->on_avc_decoder_configuration_record(sps, pps);
        };
        this->parser.on_hevc_decoder_configuration_record = [this](const std::vector<std::uint8_t>& vps, const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps) -> bool {
            return this->on_hevc_decoder_configuration_record(vps, sps, pps);
        };
        this->parser.on_audio_specific_config = [this](const audio_special_config& asc) -> bool {
            return this->on_audio_specific_config(asc);
        };
    }
    this->parser.on_audio_sample = [this](audio_sample&& sample) -> bool {
        return this->on_audio_sample(std::move(sample));
    };
    this->parser.on_video_sample = [this](video_sample&& sample) -> bool {
        return this->on_video_sample(std::move(sample));
    };
}

void flv_player::unregister_callback_functions()
{
    this->parser.on_script_tag = nullptr;
    this->parser.on_avc_decoder_configuration_record = nullptr;
    this->parser.on_audio_specific_config = nullptr;
    this->parser.on_audio_sample = nullptr;
    this->parser.on_video_sample = nullptr;
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

std::int64_t flv_player::adjust_sample_timestamp(std::int64_t timestamp)
{
    if (this->can_seek) {
        return timestamp;
    }
    if (timestamp > this->first_sample_timestamp) {
        return timestamp - this->first_sample_timestamp;
    }
    return 0;
}

} // namespace dawn_player
