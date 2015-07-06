/*
*    flv_media_stream_source.cpp:
*
*    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
*
*/

#include "pch.h"

#include <algorithm>
#include <memory>
#include <string>

#include "flv_media_stream_source.hpp"
#include "flv_player.hpp"

using namespace Platform::Collections;
using namespace Windows::Media::MediaProperties;

namespace dawn_player {

flv_media_stream_source::flv_media_stream_source()
{
}

void flv_media_stream_source::init(flv_player^ player, MediaStreamSource^ mss)
{
    this->player = player;
    this->mss = mss;
    starting_event_token = this->mss->Starting += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceStartingEventArgs^>(this, &flv_media_stream_source::on_starting);
    sample_requested_event_token = this->mss->SampleRequested += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceSampleRequestedEventArgs^>(this, &flv_media_stream_source::on_sample_requested);
}

IAsyncOperation<flv_media_stream_source^>^ flv_media_stream_source::create_async(IRandomAccessStream^ random_access_stream)
{
    return create_async(random_access_stream, true);
}

IAsyncOperation<flv_media_stream_source^>^ flv_media_stream_source::create_async(IRandomAccessStream^ random_access_stream, bool stream_can_seek)
{
    auto player = ref new flv_player();
    player->set_source(random_access_stream);
    auto media_info = ref new Map<Platform::String^, Platform::String^>();
    auto shared_task = std::make_shared<concurrency::task<open_result>>(concurrency::create_task(player->open_async(media_info)));
    return concurrency::create_async([=](concurrency::cancellation_token ct) -> flv_media_stream_source^ {
        auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
        auto callback_token = ct.register_callback([player, cancel_flag]() {
            bool exp = false;
            if (cancel_flag->compare_exchange_strong(exp, true)) {
                player->close();
            }
        });
        auto result = shared_task->get();
        ct.deregister_callback(callback_token);
        bool exp = false;
        bool is_canceled = !cancel_flag->compare_exchange_strong(exp, true);
        if (is_canceled) {
            throw ref new Platform::OperationCanceledException("Operation canceled.");
        }
        if (result != open_result::ok) {
            player->close();
            throw ref new Platform::FailureException("Failed to open FLV file.");
        }
        auto acpd_w = media_info->Lookup(L"AudioCodecPrivateData")->ToString();
        std::string acpd;
        std::transform(acpd_w->Begin(), acpd_w->End(), std::back_inserter(acpd), [](wchar_t ch) -> char {
            return static_cast<char>(ch);
        });
        unsigned int channel_count = std::stol(acpd.substr(4, 2), 0, 16) + std::stol(acpd.substr(6, 2), 0, 16) * 0x100;
        unsigned int sample_rate = std::stol(acpd.substr(8, 2), 0, 16) + std::stol(acpd.substr(10, 2), 0, 16) * 0x100 +
            std::stol(acpd.substr(12, 2), 0, 16) * 0x10000 + std::stol(acpd.substr(14, 2), 0, 16) * 0x1000000;
        unsigned int bit_rate = sample_rate * (std::stol(acpd.substr(28, 2), 0, 16) + std::stol(acpd.substr(30, 2), 0, 16) * 0x100);
        auto aep = AudioEncodingProperties::CreateAac(sample_rate, channel_count, bit_rate);
        auto asd = ref new AudioStreamDescriptor(aep);
        auto vep = VideoEncodingProperties::CreateH264();
        auto video_width = std::stoul(media_info->Lookup("Width")->ToString()->Data());
        auto video_height = std::stoul(media_info->Lookup("Width")->ToString()->Data());
        // It seems that H.264 only supports even numbered dimensions.
        vep->Width = video_width - (video_width % 2);
        vep->Height = video_height - (video_height % 2);
        auto vsd = ref new VideoStreamDescriptor(vep);
        auto mss = ref new MediaStreamSource(asd);
        mss->AddStreamDescriptor(vsd);
        mss->CanSeek = stream_can_seek && (media_info->Lookup(L"CanSeek")->ToString() == L"True");
        // Set BufferTime to 0 to improve seek experience in Debug mode
        mss->BufferTime = TimeSpan{ 0 };
        if (media_info->HasKey(L"Duration")) {
            mss->Duration = TimeSpan{ std::stoll(media_info->Lookup(L"Duration")->ToString()->Data()) };
        }
        auto flv_mss = ref new flv_media_stream_source();
        flv_mss->init(player, mss);
        return flv_mss;
    });
}

MediaStreamSource^ flv_media_stream_source::unwrap()
{
    return this->mss;
}

flv_media_stream_source::~flv_media_stream_source()
{
    if (this->player) {
        this->player->close();
        this->player = nullptr;
    }
    if (this->mss) {
        this->mss->Starting -= this->starting_event_token;
        this->mss->SampleRequested -= this->sample_requested_event_token;
        this->mss = nullptr;
    }
}

void flv_media_stream_source::on_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args)
{
    auto request = args->Request;
    auto start_position = request->StartPosition;
    if (start_position == nullptr) {
        request->SetActualStartPosition(TimeSpan{ this->player->get_position() });
    }
    else if (std::abs(start_position->Value.Duration - this->player->get_position()) < 10000000) {
        // No seek if interval less than 1 second
        request->SetActualStartPosition(TimeSpan{ this->player->get_position() });
    }
    else {
        auto deferral = request->GetDeferral();
        concurrency::create_task(this->player->begin_seek(start_position->Value.Duration)).then([=](concurrency::task<std::int64_t> task) {
            auto seek_to_time = task.get();
            if (seek_to_time == -1) {
                return;
            }
            request->SetActualStartPosition(TimeSpan{ seek_to_time });
            deferral->Complete();
            this->player->end_seek();
        });
    }
}

void flv_media_stream_source::on_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args)
{
    auto request = args->Request;
    auto deferral = request->GetDeferral();
    if (request->StreamDescriptor->GetType()->FullName == AudioStreamDescriptor::typeid->FullName) {
        auto sample_info = ref new Map<Platform::String^, Platform::Object^>();
        concurrency::create_task(this->player->get_sample_async(sample_type::audio, sample_info)).then([=](concurrency::task<get_sample_result> task) {
            auto result = task.get();
            if (result == get_sample_result::ok) {
                auto sample = MediaStreamSample::CreateFromBuffer(dynamic_cast<Buffer^>(sample_info->Lookup(L"Data")),
                    TimeSpan{ std::stoll(sample_info->Lookup(L"Timestamp")->ToString()->Data()) });
                request->Sample = sample;
            }
            else if (result == get_sample_result::eos) {
                request->Sample = nullptr;
            }
            else if (result == get_sample_result::error) {
                sender->NotifyError(MediaStreamSourceErrorStatus::Other);
                return;
            }
            else {
                return;
            }
            deferral->Complete();
        });
    }
    else if (request->StreamDescriptor->GetType()->FullName == VideoStreamDescriptor::typeid->FullName) {
        auto sample_info = ref new Map<Platform::String^, Platform::Object^>();
        concurrency::create_task(this->player->get_sample_async(sample_type::video, sample_info)).then([=](concurrency::task<get_sample_result> task) {
            auto result = task.get();
            if (result == get_sample_result::ok) {
                auto sample = MediaStreamSample::CreateFromBuffer(dynamic_cast<Buffer^>(sample_info->Lookup(L"Data")),
                    TimeSpan{ std::stoll(sample_info->Lookup(L"Timestamp")->ToString()->Data()) });
                sample->DecodeTimestamp = TimeSpan{ std::stoll(sample_info->Lookup(L"DecodeTimestamp")->ToString()->Data()) };
                sample->KeyFrame = sample_info->Lookup(L"KeyFrame")->ToString() == L"True";
                request->Sample = sample;
            }
            else if (result == get_sample_result::eos) {
                request->Sample = nullptr;
            }
            else if (result == get_sample_result::error) {
                sender->NotifyError(MediaStreamSourceErrorStatus::Other);
                return;
            }
            else {
                return;
            }
            deferral->Complete();
        });
    }
}

} // namespace dawn_player
