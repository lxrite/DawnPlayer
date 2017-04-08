/*
 *    flv_player.cpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "amf_decode.hpp"
#include "error.hpp"
#include "flv_player.hpp"

namespace dawn_player {

static const size_t BUFFER_QUEUE_DEFICIENT_SIZE = 50;

flv_player::flv_player(const std::shared_ptr<task_service>& tsk_service)
    : tsk_service(tsk_service)
    , is_video_cfg_read(false)
    , is_audio_cfg_read(false)
    , position(0)
    , is_end_of_stream(false)
    , is_error_ocurred(false)
    , is_sample_reading(false)
    , last_seek_to_time(0)
    , start_position(0)
    , is_closed(false)
{
}

flv_player::~flv_player()
{
    video_file_stream = nullptr;
}

void flv_player::set_source(IRandomAccessStream^ random_access_stream)
{
    this->video_file_stream = random_access_stream;
}

task<std::map<std::string, std::string>> flv_player::open()
{
    auto tce = task_completion_event<std::map<std::string, std::string>>();
    auto result_task = task<std::map<std::string, std::string>>(tce);
    auto self(this->shared_from_this());
    create_async([this, self, tce]() {
        return this->parse_header()
        .then([this, self, tce](task<void> tsk) {
            this->tsk_service->post_task([this, self, tce, tsk]() {
                try {
                    tsk.get();
                }
                catch (...) {
                    tce.set_exception(std::current_exception());
                    return;
                }
                create_async([this, self, tce]() {
                    return this->parse_meta_data()
                    .then([this, self, tce](task<void> tsk) {
                        try {
                            tsk.get();
                        }
                        catch (...) {
                            tce.set_exception(std::current_exception());
                            return;
                        }
                        try {
                            tce.set(this->get_video_info());
                        }
                        catch (...) {
                            tce.set_exception(std::current_exception());
                        }
                    });
                });
            });
        });
    });
    return result_task;
}

std::int64_t flv_player::get_start_position() const
{
    return this->start_position;
}

task<audio_sample> flv_player::get_audio_sample()
{
    auto tce = task_completion_event<audio_sample>();
    auto result_task = task<audio_sample>(tce);
    this->audio_sample_tce_queue.push_back(tce);
    this->deliver_samples();
    return result_task;
}

task<video_sample> flv_player::get_video_sample()
{
    auto tce = task_completion_event<video_sample>();
    auto result_task = task<video_sample>(tce);
    this->video_sample_tce_queue.push_back(tce);
    this->deliver_samples();
    return result_task;
}

task<std::int64_t> flv_player::seek(std::int64_t seek_to_time)
{
    auto tce = task_completion_event<std::int64_t>();
    auto result_task = task<std::int64_t>(tce);
    this->seek_tce_queue.push(tce);
    this->last_seek_to_time = seek_to_time;
    this->handle_seek();
    return result_task;
}

task<void> flv_player::close()
{
    auto tce = task_completion_event<void>();
    auto result_task = task<void>(tce);
    this->is_closed = true;
    this->close_tce_queue.push(tce);
    this->handle_close();
    return result_task;
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

task<std::uint32_t> flv_player::read_some_data()
{
    auto tce = task_completion_event<std::uint32_t>();
    auto result_task = task<std::uint32_t>(tce);
    auto self(this->shared_from_this());
    create_async([this, self, tce]() {
        try {
            return create_task(this->video_file_stream->ReadAsync(ref new Buffer(65536), 65536, InputStreamOptions::Partial))
                .then([this, self, tce](task<IBuffer^> tsk) {
                this->tsk_service->post_task([this, self, tce, tsk]() {
                    IBuffer^ buffer = nullptr;
                    try {
                        buffer = tsk.get();
                    }
                    catch (...) {
                        tce.set_exception(std::current_exception());
                        return;
                    }
                    std::uint32_t size = buffer->Length;
                    if (size != 0) {
                        this->read_buffer.reserve(this->read_buffer.size() + size);
                        auto data_reader = DataReader::FromBuffer(buffer);
                        for (std::uint32_t i = 0; i != size; ++i) {
                            this->read_buffer.push_back(data_reader->ReadByte());
                        }
                    }
                    tce.set(size);
                });
            });
        }
        catch (Platform::ObjectDisposedException^) {
            tce.set_exception(std::runtime_error("video file stream disposed"));
            return task_from_result();
        }
    });
    return result_task;
}

task<void> flv_player::parse_header()
{
    auto tce = task_completion_event<void>();
    auto result_task = task<void>(tce);
    auto self(this->shared_from_this());
    create_async([this, self, tce]() {
        return this->read_some_data()
        .then([this, self, tce](task<std::uint32_t> tsk) {
            this->tsk_service->post_task([this, self, tce, tsk]() {
                std::uint32_t size = 0;
                try {
                    size = tsk.get();
                }
                catch (...) {
                    tce.set_exception(open_error("Read data error.", open_error_code::io_error));
                    return;
                }
                if (size == 0) {
                    tce.set_exception(open_error("Failed to parse header, end of stream.", open_error_code::parse_error));
                    return;
                }
                if (this->read_buffer.size() < this->parser.first_tag_offset()) {
                    create_async([this, self, tce]() {
                        return this->parse_header()
                        .then([this, self, tce](task<void> tsk) {
                            this->tsk_service->post_task([this, self, tce, tsk]() {
                                try {
                                    tsk.get();
                                    tce.set();
                                }
                                catch (...) {
                                    tce.set_exception(std::current_exception());
                                }
                            });
                        });
                    });
                    return;
                }
                size_t bytes_consumed = 0;
                auto parse_res = this->parser.parse_flv_header(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
                if (parse_res != parse_result::ok) {
                    tce.set_exception(open_error("Bad FLV header.", open_error_code::parse_error));
                    return;
                }
                std::memmove(this->read_buffer.data(), this->read_buffer.data() + this->parser.first_tag_offset(), this->read_buffer.size() - this->parser.first_tag_offset());
                this->read_buffer.resize(this->read_buffer.size() - this->parser.first_tag_offset());
                tce.set();
            });
        });
    });
    return result_task;
}

task<void> flv_player::parse_meta_data()
{
    auto tce = task_completion_event<void>();
    auto result_task = task<void>(tce);
    auto self(this->shared_from_this());
    create_async([this, self, tce]() {
        return read_some_data()
        .then([this, self, tce](task<std::uint32_t> tsk) {
            this->tsk_service->post_task([this, self, tce, tsk]() {
                std::uint32_t size = 0;
                try {
                    size = tsk.get();
                }
                catch (...) {
                    tce.set_exception(open_error("Read data error.", open_error_code::io_error));
                    return;
                }
                size_t bytes_consumed = 0;
                this->register_callback_functions(false);
                auto result = this->parser.parse_flv_tags(this->read_buffer.data(), this->read_buffer.size(), bytes_consumed);
                this->unregister_callback_functions();
                if (result != parse_result::ok) {
                    tce.set_exception(open_error("Bad FLV data.", open_error_code::parse_error));
                    return;
                }
                if (this->flv_meta_data && this->is_audio_cfg_read && this->is_video_cfg_read && !this->video_sample_queue.empty()) {
                    this->start_position = this->video_sample_queue.front().timestamp;
                    tce.set();
                    return;
                }
                if (size == 0) {
                    tce.set_exception(open_error("Failed to parse header, end of stream.", open_error_code::parse_error));
                    return;
                }
                create_async([this, self, tce]() {
                    return this->parse_meta_data()
                    .then([this, self, tce](task<void> tsk) {
                        this->tsk_service->post_task([this, self, tce, tsk]() {
                            try {
                                tsk.get();
                                tce.set();
                            }
                            catch (...) {
                                tce.set_exception(std::current_exception());
                            }
                        });
                    });
                });
            });
        });
    });
    return result_task;
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

    if (!this->keyframes.empty()) {
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

    // Only surpport AVC
    info["VideoFourCC"] = std::string("H264");
    info["VideoCodecPrivateData"] = video_codec_private_data;
    return info;
}

void flv_player::deliver_samples()
{
    while (true) {
        bool brk = true;
        if (!this->audio_sample_tce_queue.empty() && !this->audio_sample_queue.empty()) {
            auto sample = std::move(this->audio_sample_queue.front());
            this->audio_sample_queue.pop_front();
            auto tce = this->audio_sample_tce_queue.front();
            this->audio_sample_tce_queue.pop_front();
            tce.set(sample);
            brk = false;
        }
        if (!this->video_sample_tce_queue.empty() && !this->video_sample_queue.empty()) {
            auto sample = std::move(this->video_sample_queue.front());
            this->video_sample_queue.pop_front();
            auto tce = this->video_sample_tce_queue.front();
            this->video_sample_tce_queue.pop_front();
            tce.set(sample);
            brk = false;
            this->position = sample.timestamp;
        }
        if (brk) {
            break;
        }
    }
    if (this->is_error_ocurred) {
        if (!this->audio_sample_tce_queue.empty()) {
            auto tce = this->audio_sample_tce_queue.front();
            tce.set_exception(get_sample_error("errror ocurred", get_sample_error_code::other));
        }
        else if (!this->video_sample_tce_queue.empty()) {
            auto tce = this->video_sample_tce_queue.front();
            tce.set_exception(get_sample_error("errror ocurred", get_sample_error_code::other));
        }
        this->audio_sample_tce_queue.clear();
        this->video_sample_tce_queue.clear();
        return;
    }
    if (this->is_end_of_stream) {
        if (!this->audio_sample_tce_queue.empty()) {
            auto tce = this->audio_sample_tce_queue.front();
            this->audio_sample_tce_queue.clear();
            tce.set_exception(get_sample_error("end of stream", get_sample_error_code::end_of_stream));
        }
        if (!this->video_sample_tce_queue.empty()) {
            auto tce = this->video_sample_tce_queue.front();
            this->video_sample_tce_queue.clear();
            tce.set_exception(get_sample_error("end of stream", get_sample_error_code::end_of_stream));
        }
    }
    if ((this->audio_sample_queue.size() < BUFFER_QUEUE_DEFICIENT_SIZE || this->video_sample_queue.size() < BUFFER_QUEUE_DEFICIENT_SIZE) && !this->is_sample_reading) {
        if (!this->is_closed && !this->is_end_of_stream && !this->is_error_ocurred) {
            this->read_more_sample();
        }
    }
}

void flv_player::handle_seek()
{
    if (this->seek_tce_queue.empty()) {
        return;
    }
    if (this->is_sample_reading) {
        return;
    }
    double seek_to_time_sec = static_cast<double>(this->last_seek_to_time) / 10000000;
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
        this->video_file_stream->Seek(position);
    }
    catch (Platform::ObjectDisposedException^) {
        this->is_error_ocurred = true;
    }
    auto seek_to_time = static_cast<std::int64_t>(time * 10000000);
    while (!this->seek_tce_queue.empty()) {
        auto tce = this->seek_tce_queue.front();
        this->seek_tce_queue.pop();
        tce.set(seek_to_time);
    }
}

void flv_player::handle_close()
{
    if (this->is_closed && !this->is_sample_reading)
    {
        while (!this->close_tce_queue.empty()) {
            auto tce = this->close_tce_queue.front();
            this->close_tce_queue.pop();
            tce.set();
        }
    }
}

void flv_player::read_more_sample()
{
    assert(!this->is_sample_reading);
    this->is_sample_reading = true;
    auto self(this->shared_from_this());
    create_async([this, self]() {
        return this->read_some_data()
            .then([this, self](task<std::uint32_t> tsk) {
            this->tsk_service->post_task([this, self, tsk]() {
                std::uint32_t size = 0;
                try {
                    size = tsk.get();
                }
                catch (const std::exception&) {
                    this->is_error_ocurred = true;
                }
                if (!this->is_error_ocurred) {
                    if (size == 0) {
                        this->is_end_of_stream = true;
                    }
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

                this->is_sample_reading = false;
                this->handle_close();
                if (this->is_closed) {
                    return;
                }
                this->handle_seek();
                this->deliver_samples();
            });
        });
    });
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
    this->sps = sps;
    this->pps = pps;
    this->video_codec_private_data;
    std::uint8_t prefix[3] = { 0x00, 0x00, 0x01 };
    this->video_codec_private_data += this->uint8_to_hex_string(prefix, sizeof(prefix));
    this->video_codec_private_data += this->uint8_to_hex_string(sps.data(), sps.size());
    this->video_codec_private_data += this->uint8_to_hex_string(prefix, sizeof(prefix));
    this->video_codec_private_data += this->uint8_to_hex_string(pps.data(), pps.size());
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
    this->audio_sample_queue.emplace_back(std::move(sample));
    return true;
}

bool flv_player::on_video_sample(video_sample&& sample)
{
    if (!this->is_video_cfg_read) {
        return false;
    }
    this->video_sample_queue.emplace_back(std::move(sample));
    return true;
}

void flv_player::register_callback_functions(bool sample_only)
{
    if (sample_only) {
        this->parser.on_script_tag = nullptr;
        this->parser.on_avc_decoder_configuration_record = nullptr;
        this->parser.on_audio_specific_config = nullptr;
    }
    else {
        this->parser.on_script_tag = [this](std::shared_ptr<amf_base> name, std::shared_ptr<amf_base> value) -> bool {
            return this->on_script_tag(name, value);
        };
        this->parser.on_avc_decoder_configuration_record = [this](const std::vector<std::uint8_t>& sps, const std::vector<std::uint8_t>& pps) -> bool {
            return this->on_avc_decoder_configuration_record(sps, pps);
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

} // namespace dawn_player
