/*
*    flv_media_stream_source.cpp:
*
*    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#include <algorithm>
#include <string>

#include "default_task_service.hpp"
#include "error.hpp"
#include "flv_media_stream_source.hpp"
#include "flv_player.hpp"
#include "io.hpp"

using namespace concurrency;
using namespace dawn_player;
using namespace dawn_player::io;
using namespace Windows::Media::MediaProperties;

namespace DawnPlayer {

FlvMediaStreamSource::FlvMediaStreamSource()
{
}

void FlvMediaStreamSource::init(const std::shared_ptr<flv_player>& player, MediaStreamSource^ mss)
{
    this->player = player;
    this->mss = mss;
    this->tsk_service = this->player->get_task_service();
    starting_event_token = this->mss->Starting += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceStartingEventArgs^>(this, &FlvMediaStreamSource::on_starting);
    sample_requested_event_token = this->mss->SampleRequested += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceSampleRequestedEventArgs^>(this, &FlvMediaStreamSource::on_sample_requested);
}

IAsyncOperation<FlvMediaStreamSource^>^ FlvMediaStreamSource::CreateFromRandomAccessStreamAsync(IRandomAccessStream^ randomAccessStream)
{
    auto stream_proxy = std::make_shared<ramdon_access_read_stream_proxy>(randomAccessStream);
    auto tce = task_completion_event<FlvMediaStreamSource^>();
    auto result_task = task<FlvMediaStreamSource^>(tce);
    auto tsk_service = std::make_shared<default_task_service>();
    tsk_service->post_task([tsk_service, stream_proxy, tce]() {
        auto player = std::make_shared<flv_player>(tsk_service, stream_proxy);
        player->open()
        .then([tsk_service, player, tce](task<std::map<std::string, std::string>> tsk) {
            tsk_service->post_task([player, tce, tsk]() {
                std::map<std::string, std::string> info;
                try {
                    info = tsk.get();
                }
                catch (const open_error&) {
                    tce.set_exception(ref new Platform::FailureException("Failed to open FLV file."));
                    return;
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
                tce.set(flv_mss);
            });
        });
    });
    return create_async([result_task]() {
        return result_task;
    });
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
        this->tsk_service->post_task([player]() {
            create_async([player]() {
                return player->close();
            });
        });
    }
    if (this->mss) {
        this->mss->Starting -= this->starting_event_token;
        this->mss->SampleRequested -= this->sample_requested_event_token;
        this->mss = nullptr;
    }
}

void FlvMediaStreamSource::on_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args)
{
    auto request = args->Request;
    auto deferral = request->GetDeferral();
    auto tsk_service = this->tsk_service;
    auto wp_player = std::weak_ptr<flv_player>(this->player);
    tsk_service->post_task([tsk_service, wp_player, request, deferral]() {
        auto start_position = request->StartPosition;
        if (start_position == nullptr) {
            auto player = wp_player.lock();
            if (player) {
                request->SetActualStartPosition(TimeSpan{ player->get_start_position() });
                deferral->Complete();
            }
        }
        else {
            create_async([tsk_service, wp_player, start_position, request, deferral]() {
                auto player = wp_player.lock();
                if (!player) {
                    return task_from_result();
                }
                return player->seek(start_position->Value.Duration)
                .then([tsk_service, request, deferral](task<std::int64_t> tsk) {
                    tsk_service->post_task([tsk, request, deferral]() {
                        auto seek_to_time = tsk.get();
                        request->SetActualStartPosition(TimeSpan{ seek_to_time });
                        deferral->Complete();
                    });
                });
            });
        }
    });
}

void FlvMediaStreamSource::on_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args)
{
    auto request = args->Request;
    auto deferral = request->GetDeferral();
    auto tsk_service = this->tsk_service;
    auto wp_player = std::weak_ptr<flv_player>(this->player);
    tsk_service->post_task([tsk_service, wp_player, sender, request, deferral]() {
        if (request->StreamDescriptor->GetType()->FullName == AudioStreamDescriptor::typeid->FullName) {
            create_async([tsk_service, wp_player, sender, request, deferral]() {
                auto player = wp_player.lock();
                if (!player) {
                    return task_from_result();
                }
                return player->get_audio_sample()
                    .then([tsk_service, sender, request, deferral](task<audio_sample> tsk) {
                    tsk_service->post_task([sender, request, deferral, tsk]() {
                        try {
                            audio_sample sample = tsk.get();
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
                            else {
                                sender->NotifyError(MediaStreamSourceErrorStatus::Other);
                            }
                        }
                        deferral->Complete();
                    });
                });
            }); 
        }
        else if (request->StreamDescriptor->GetType()->FullName == VideoStreamDescriptor::typeid->FullName) {
            create_async([tsk_service, wp_player, sender, request, deferral]() {
                auto player = wp_player.lock();
                if (!player) {
                    return task_from_result();
                }
                return player->get_video_sample()
                .then([tsk_service, wp_player, sender, request, deferral](task<video_sample> tsk) {
                    tsk_service->post_task([wp_player, sender, request, deferral, tsk]() {
                        auto player = wp_player.lock();
                        if (!player) {
                            return;
                        }
                        try {
                            video_sample sample = tsk.get();
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
                            else {
                                sender->NotifyError(MediaStreamSourceErrorStatus::Other);
                            }
                        }
                        deferral->Complete();
                    });
                });
            });
        }
    });
}

} // namespace DawnPlayer
