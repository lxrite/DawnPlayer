/*
 *    FlvMediaStreamSource.cpp:
 *
 *    Copyright (C) 2024-2025 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#include "FlvMediaStreamSource.h"
#include "FlvMediaStreamSource.g.cpp"

#include <winrt/base.h>
#include <ppltasks.h>
#include <winerror.h>

#include "core/dawn_player/default_task_service.hpp"
#include "core/dawn_player/error.hpp"

using namespace concurrency;
using namespace dawn_player;

namespace winrt::DawnPlayer::implementation
{
    FlvMediaStreamSource::FlvMediaStreamSource(const std::shared_ptr<flv_player>& player, MediaStreamSource mss)
        : player_(player)
        , mss_(mss)
    {
        this->starting_event_token = this->mss_.Starting([this](MediaStreamSource sender, MediaStreamSourceStartingEventArgs args) {
            this->on_starting(sender, args);
        });
        this->sample_requested_event_token = this->mss_.SampleRequested([this](MediaStreamSource sender, MediaStreamSourceSampleRequestedEventArgs args) {
            this->on_sample_requested(sender, args);
        });
    }

    IAsyncOperation<DawnPlayer::FlvMediaStreamSource> FlvMediaStreamSource::CreateFromInputStreamAsync(IInputStream inputStream)
    {
        auto stream_proxy = std::make_shared<input_read_stream_proxy>(inputStream);
        return FlvMediaStreamSource::create_from_read_stream_proxy_async(stream_proxy);
    }

    IAsyncOperation<DawnPlayer::FlvMediaStreamSource> FlvMediaStreamSource::CreateFromRandomAccessStreamAsync(IRandomAccessStream randomAccessStream)
    {
        auto stream_proxy = std::make_shared<ramdon_access_read_stream_proxy>(randomAccessStream);
        return FlvMediaStreamSource::create_from_read_stream_proxy_async(stream_proxy);
    }

    MediaStreamSource FlvMediaStreamSource::Source()
    {
        return this->mss_;
    }

    FlvMediaStreamSource::~FlvMediaStreamSource()
    {
        this->Close();
    }

    void FlvMediaStreamSource::Close() {
        if (this->player_) {
            auto player = this->player_;
            this->player_ = nullptr;
            player->close();
        }
        if (this->mss_) {
            if (this->starting_event_token.has_value()) {
                this->mss_.Starting(this->starting_event_token.value());
                this->starting_event_token.reset();
            }
            if (this->sample_requested_event_token.has_value()) {
                this->mss_.SampleRequested(this->sample_requested_event_token.value());
                this->sample_requested_event_token.reset();
            }
            this->mss_ = nullptr;
        }
    }

    IAsyncOperation<DawnPlayer::FlvMediaStreamSource> FlvMediaStreamSource::create_from_read_stream_proxy_async(std::shared_ptr<read_stream_proxy> stream_proxy)
    {
        winrt::apartment_context caller;
        co_await winrt::resume_background();
        auto tsk_service = std::make_shared<default_task_service>();
        auto player = std::make_shared<flv_player>(tsk_service, stream_proxy);
        std::map<std::string, std::string> info;
        try {
            info = player->open().get();
        }
        catch (const open_error&) {
            winrt::throw_hresult(E_FAIL);
        }
        std::string acpd = info["AudioCodecPrivateData"];
        std::uint32_t audio_format_tag = std::stol(acpd.substr(0, 2), 0, 16) + std::stol(acpd.substr(2, 2), 0, 16) * 0x100;
        unsigned int channel_count = std::stol(acpd.substr(4, 2), 0, 16) + std::stol(acpd.substr(6, 2), 0, 16) * 0x100;
        unsigned int sample_rate = std::stol(acpd.substr(8, 2), 0, 16) + std::stol(acpd.substr(10, 2), 0, 16) * 0x100 +
            std::stol(acpd.substr(12, 2), 0, 16) * 0x10000 + std::stol(acpd.substr(14, 2), 0, 16) * 0x1000000;
        unsigned int bit_rate = sample_rate * (std::stol(acpd.substr(28, 2), 0, 16) + std::stol(acpd.substr(30, 2), 0, 16) * 0x100);
        AudioEncodingProperties aep;
        if (audio_format_tag == 0x0055) {
            aep = AudioEncodingProperties::CreateMp3(sample_rate, channel_count, bit_rate);
        }
        else {
            aep = AudioEncodingProperties::CreateAac(sample_rate, channel_count, bit_rate);
        }
        auto asd = AudioStreamDescriptor(aep);
        auto vep = FlvMediaStreamSource::CreateVideoEncodingProperties(player->get_video_codec());
        auto video_width = std::stoul(info["Width"]);
        auto video_height = std::stoul(info["Height"]);
        // It seems that H.264 only supports even numbered dimensions.
        vep.Width(video_width - (video_width % 2));
        vep.Height(video_height - (video_height % 2));
        auto vsd = VideoStreamDescriptor(vep);
        auto mss = MediaStreamSource(asd);
        mss.AddStreamDescriptor(vsd);
        mss.CanSeek(info["CanSeek"] == "True");
        // Set BufferTime to 0 to improve seek experience in Debug mode
        mss.BufferTime(TimeSpan{ 0 });
        auto iter_duration = info.find("Duration");
        if (iter_duration != info.end()) {
            mss.Duration(TimeSpan{ std::stoll(std::get<1>(*iter_duration)) });
        }
        co_await caller;
        co_return winrt::make<FlvMediaStreamSource>(player, mss);
    }

    void FlvMediaStreamSource::on_starting(MediaStreamSource sender, MediaStreamSourceStartingEventArgs args)
    {
        this->handle_starting(sender, args);
    }

    void FlvMediaStreamSource::on_sample_requested(MediaStreamSource sender, MediaStreamSourceSampleRequestedEventArgs args)
    {
        this->handle_sample_requested(sender, args);
    }

    void FlvMediaStreamSource::handle_starting(MediaStreamSource sender, MediaStreamSourceStartingEventArgs args)
    {
        auto request = args.Request();
        auto start_position = request.StartPosition();
        auto player = this->player_;
        if (start_position != nullptr && player) {
            try {
                auto seek_to_time = player->seek(start_position.Value().count()).get();
                request.SetActualStartPosition(TimeSpan{ seek_to_time });
            }
            catch (const seek_error&) {
            }
        }
    }

    void FlvMediaStreamSource::handle_sample_requested(MediaStreamSource sender, MediaStreamSourceSampleRequestedEventArgs args)
    {
        auto request = args.Request();
        auto player = this->player_;
        if (player) {
            if (request.StreamDescriptor().try_as<AudioStreamDescriptor>()) {
                try {
                    auto sample = player->get_audio_sample().get();
                    auto data_writer = DataWriter();
                    for (auto byte : sample.data) {
                        data_writer.WriteByte(byte);
                    }
                    auto stream_sample = MediaStreamSample::CreateFromBuffer(data_writer.DetachBuffer(), TimeSpan{ sample.timestamp });
                    request.Sample(stream_sample);
                }
                catch (const get_sample_error& gse) {
                    if (gse.code() == get_sample_error_code::end_of_stream) {
                        request.Sample(nullptr);
                    }
                    else if (gse.code() != get_sample_error_code::cancel) {
                        sender.NotifyError(MediaStreamSourceErrorStatus::Other);
                    }
                }
            }
            else if (request.StreamDescriptor().try_as<VideoStreamDescriptor>()) {
                try {
                    video_sample sample = player->get_video_sample().get();
                    auto data_writer = DataWriter();
                    if (sample.is_key_frame) {
                        if (player->get_video_codec() == video_codec::hevc) {
                            const auto& vps = player->get_vps();
                            if (!vps.empty()) {
                                data_writer.WriteByte(0);
                                data_writer.WriteByte(0);
                                data_writer.WriteByte(1);
                                for (auto byte : vps) {
                                    data_writer.WriteByte(byte);
                                }
                            }
                        }
                        const auto& sps = player->get_sps();
                        if (!sps.empty()) {
                            data_writer.WriteByte(0);
                            data_writer.WriteByte(0);
                            data_writer.WriteByte(1);
                            for (auto byte : sps) {
                                data_writer.WriteByte(byte);
                            }
                        }
                        const auto& pps = player->get_pps();
                        if (!pps.empty()) {
                            data_writer.WriteByte(0);
                            data_writer.WriteByte(0);
                            data_writer.WriteByte(1);
                            for (auto byte : pps) {
                                data_writer.WriteByte(byte);
                            }
                        }
                    }
                    for (auto byte : sample.data) {
                        data_writer.WriteByte(byte);
                    }
                    auto stream_sample = MediaStreamSample::CreateFromBuffer(data_writer.DetachBuffer(), TimeSpan{ sample.timestamp });
                    stream_sample.DecodeTimestamp(TimeSpan{sample.dts});
                    stream_sample.KeyFrame(sample.is_key_frame);
                    request.Sample(stream_sample);
                }
                catch (const get_sample_error& gse) {
                    if (gse.code() == get_sample_error_code::end_of_stream) {
                        request.Sample(nullptr);
                    }
                    else if (gse.code() != get_sample_error_code::cancel) {
                        sender.NotifyError(MediaStreamSourceErrorStatus::Other);
                    }
                }
            }
        }
    }

    VideoEncodingProperties FlvMediaStreamSource::CreateVideoEncodingProperties(video_codec vc)
    {
        switch (vc) {
        case video_codec::hevc:
            return VideoEncodingProperties::CreateHevc();
        default:
            return VideoEncodingProperties::CreateH264();
        }
        
    }
}
