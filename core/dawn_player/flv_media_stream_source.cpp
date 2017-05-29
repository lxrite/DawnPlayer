/*
*    flv_media_stream_source.cpp:
*
*    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#include <algorithm>
#include <string>

#include <ppltasks.h>

#include "default_task_service.hpp"
#include "error.hpp"
#include "flv_media_stream_source.hpp"
#include "flv_player.hpp"

using namespace concurrency;
using namespace dawn_player;
using namespace Windows::Media::MediaProperties;

namespace DawnPlayer {

FlvMediaStreamSource::FlvMediaStreamSource()
{
}

void FlvMediaStreamSource::init(const std::shared_ptr<flv_player>& player, MediaStreamSource^ mss)
{
    this->player = player;
    this->mss = mss;
    starting_event_token = this->mss->Starting += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceStartingEventArgs^>(this, &FlvMediaStreamSource::on_starting);
    sample_requested_event_token = this->mss->SampleRequested += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceSampleRequestedEventArgs^>(this, &FlvMediaStreamSource::on_sample_requested);
}

IAsyncOperation<FlvMediaStreamSource^>^ FlvMediaStreamSource::CreateFromInputStreamAsync(IInputStream^ inputStream)
{
    auto stream_proxy = std::make_shared<input_read_stream_proxy>(inputStream);
    return create_from_read_stream_proxy_async(stream_proxy);
}

IAsyncOperation<FlvMediaStreamSource^>^ FlvMediaStreamSource::CreateFromRandomAccessStreamAsync(IRandomAccessStream^ randomAccessStream)
{
    auto stream_proxy = std::make_shared<ramdon_access_read_stream_proxy>(randomAccessStream);
    return create_from_read_stream_proxy_async(stream_proxy);
}

MediaStreamSource^ FlvMediaStreamSource::Source::get()
{
    return this->mss;
}

FlvMediaStreamSource::~FlvMediaStreamSource()
{
    if (this->player) {
        auto player = this->player;
        this->player = nullptr;
        player->close();
    }
    if (this->mss) {
        this->mss->Starting -= this->starting_event_token;
        this->mss->SampleRequested -= this->sample_requested_event_token;
        this->mss = nullptr;
    }
}

IAsyncOperation<FlvMediaStreamSource^>^ FlvMediaStreamSource::create_from_read_stream_proxy_async(const std::shared_ptr<read_stream_proxy>& stream_proxy)
{
    return create_async([stream_proxy]() {
        auto tsk_service = std::make_shared<default_task_service>();
        auto player = std::make_shared<flv_player>(tsk_service, stream_proxy);
        std::map<std::string, std::string> info;
        try {
            info = player->open().get();
        }
        catch (const open_error&) {
            throw ref new Platform::FailureException("Failed to open FLV file.");
        }
        std::string acpd = info["AudioCodecPrivateData"];
        unsigned int channel_count = std::stol(acpd.substr(4, 2), 0, 16) + std::stol(acpd.substr(6, 2), 0, 16) * 0x100;
        unsigned int sample_rate = std::stol(acpd.substr(8, 2), 0, 16) + std::stol(acpd.substr(10, 2), 0, 16) * 0x100 +
            std::stol(acpd.substr(12, 2), 0, 16) * 0x10000 + std::stol(acpd.substr(14, 2), 0, 16) * 0x1000000;
        unsigned int bit_rate = sample_rate * (std::stol(acpd.substr(28, 2), 0, 16) + std::stol(acpd.substr(30, 2), 0, 16) * 0x100);
        auto aep = AudioEncodingProperties::CreateAac(sample_rate, channel_count, bit_rate);
        auto asd = ref new AudioStreamDescriptor(aep);
        auto vep = VideoEncodingProperties::CreateH264();
        auto video_width = std::stoul(info["Width"]);
        auto video_height = std::stoul(info["Height"]);
        // It seems that H.264 only supports even numbered dimensions.
        vep->Width = video_width - (video_width % 2);
        vep->Height = video_height - (video_height % 2);
        auto vsd = ref new VideoStreamDescriptor(vep);
        auto mss = ref new MediaStreamSource(asd);
        mss->AddStreamDescriptor(vsd);
        mss->CanSeek = info["CanSeek"] == "True";
        // Set BufferTime to 0 to improve seek experience in Debug mode
        mss->BufferTime = TimeSpan{ 0 };
        auto iter_duration = info.find("Duration");
        if (iter_duration != info.end()) {
            mss->Duration = TimeSpan{ std::stoll(std::get<1>(*iter_duration)) };
        }
        auto flv_mss = ref new FlvMediaStreamSource();
        flv_mss->init(player, mss);
        return flv_mss;
    });
}

void FlvMediaStreamSource::on_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args)
{
    this->handle_starting(sender, args).get();
}

void FlvMediaStreamSource::on_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args)
{
    this->handle_sample_requested(sender, args).get();
}

std::future<void> FlvMediaStreamSource::handle_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args)
{
    auto request = args->Request;
    auto start_position = request->StartPosition;
    auto player = this->player;
    if (start_position != nullptr && player) {
        try {
            auto seek_to_time = co_await player->seek(start_position->Value.Duration);
            request->SetActualStartPosition(TimeSpan{ seek_to_time });
        }
        catch (const seek_error&) {
        }
    }
}

std::future<void> FlvMediaStreamSource::handle_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args)
{
    auto request = args->Request;
    auto player = this->player;
    if (player) {
        if (request->StreamDescriptor->GetType()->FullName == AudioStreamDescriptor::typeid->FullName) {
            try {
                auto sample = co_await player->get_audio_sample();
                auto data_writer = ref new DataWriter();
                for (auto byte : sample.data) {
                    data_writer->WriteByte(byte);
                }
                auto stream_sample = MediaStreamSample::CreateFromBuffer(data_writer->DetachBuffer(), TimeSpan{ sample.timestamp });
                request->Sample = stream_sample;
            }
            catch (const get_sample_error& gse) {
                if (gse.code() == get_sample_error_code::end_of_stream) {
                    request->Sample = nullptr;
                }
                else if (gse.code() != get_sample_error_code::cancel){
                    sender->NotifyError(MediaStreamSourceErrorStatus::Other);
                }
            }
        }
        else if (request->StreamDescriptor->GetType()->FullName == VideoStreamDescriptor::typeid->FullName) {
            try {
                video_sample sample = co_await player->get_video_sample();
                auto data_writer = ref new DataWriter();
                if (sample.is_key_frame) {
                    const auto& sps = player->get_sps();
                    if (!sps.empty()) {
                        data_writer->WriteByte(0);
                        data_writer->WriteByte(0);
                        data_writer->WriteByte(1);
                        for (auto byte : sps) {
                            data_writer->WriteByte(byte);
                        }
                    }
                    const auto& pps = player->get_pps();
                    if (!pps.empty()) {
                        data_writer->WriteByte(0);
                        data_writer->WriteByte(0);
                        data_writer->WriteByte(1);
                        for (auto byte : pps) {
                            data_writer->WriteByte(byte);
                        }
                    }
                }
                for (auto byte : sample.data) {
                    data_writer->WriteByte(byte);
                }
                auto stream_sample = MediaStreamSample::CreateFromBuffer(data_writer->DetachBuffer(), TimeSpan{ sample.timestamp });
                stream_sample->DecodeTimestamp = TimeSpan{ sample.dts };
                stream_sample->KeyFrame = sample.is_key_frame;
                request->Sample = stream_sample;
            }
            catch (const get_sample_error& gse) {
                if (gse.code() == get_sample_error_code::end_of_stream) {
                    request->Sample = nullptr;
                }
                else if (gse.code() != get_sample_error_code::cancel){
                    sender->NotifyError(MediaStreamSourceErrorStatus::Other);
                }
            }
        }
    }
}

} // namespace DawnPlayer
