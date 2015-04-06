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
    closed_event_token = this->mss->Closed += ref new TypedEventHandler<MediaStreamSource^, MediaStreamSourceClosedEventArgs^>(this, &flv_media_stream_source::on_closed);
}

IAsyncOperation<flv_media_stream_source^>^ flv_media_stream_source::create_async(IRandomAccessStream^ random_access_stream)
{
    auto player = ref new flv_player();
    player->set_source(random_access_stream);
    auto media_info = ref new Map<Platform::String^, Platform::String^>();
    auto shared_task = std::make_shared<concurrency::task<open_result>>(concurrency::create_task(player->open_async(media_info)));
    return concurrency::create_async([=]() -> flv_media_stream_source^ {
        auto result = shared_task->get();
        if (result != open_result::ok) {
            player->close();
            return nullptr;
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
        vep->Height = std::stoul(media_info->Lookup("Height")->ToString()->Data());
        vep->Width = std::stoul(media_info->Lookup("Width")->ToString()->Data());
        auto vsd = ref new VideoStreamDescriptor(vep);
        auto mss = ref new MediaStreamSource(asd);
        mss->AddStreamDescriptor(vsd);
        mss->CanSeek = media_info->Lookup(L"CanSeek")->ToString() == L"True";
        mss->Duration = TimeSpan{ std::stoll(media_info->Lookup(L"Duration")->ToString()->Data()) };
        auto flv_mss = ref new flv_media_stream_source();
        flv_mss->init(player, mss);
        return flv_mss;
    });
}

MediaStreamSource^ flv_media_stream_source::unwrap()
{
    return this->mss;
}

void flv_media_stream_source::on_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args)
{
    auto request = args->Request;
    auto start_position = request->StartPosition;
    if (start_position == nullptr) {
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
            if (result != get_sample_result::ok) {
                return;
            }
            auto sample = MediaStreamSample::CreateFromBuffer(dynamic_cast<Buffer^>(sample_info->Lookup(L"Data")),
                TimeSpan{ std::stoll(sample_info->Lookup(L"Timestamp")->ToString()->Data()) });
            request->Sample = sample;
            deferral->Complete();
        });
    }
    else if (request->StreamDescriptor->GetType()->FullName == VideoStreamDescriptor::typeid->FullName) {
        auto sample_info = ref new Map<Platform::String^, Platform::Object^>();
        concurrency::create_task(this->player->get_sample_async(sample_type::video, sample_info)).then([=](concurrency::task<get_sample_result> task) {
            auto result = task.get();
            if (result != get_sample_result::ok) {
                return;
            }
            auto sample = MediaStreamSample::CreateFromBuffer(dynamic_cast<Buffer^>(sample_info->Lookup(L"Data")),
                TimeSpan{ std::stoll(sample_info->Lookup(L"Timestamp")->ToString()->Data()) });
            sample->DecodeTimestamp = TimeSpan{ std::stoll(sample_info->Lookup(L"DecodeTimestamp")->ToString()->Data()) };
            sample->KeyFrame = sample_info->Lookup(L"KeyFrame")->ToString() == L"True";
            request->Sample = sample;
            deferral->Complete();
        });
    }
}

void flv_media_stream_source::on_closed(MediaStreamSource^ sender, MediaStreamSourceClosedEventArgs^ args)
{
    this->player->close();
    this->mss->Starting -= starting_event_token;
    this->mss->SampleRequested -= sample_requested_event_token;
    this->mss->Closed -= closed_event_token;
    this->player = nullptr;
}

} // namespace dawn_player
