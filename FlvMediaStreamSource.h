/*
 *    FlvMediaStreamSource.h:
 *
 *    Copyright (C) 2024 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FLV_MEDIA_STREAM_SOURCE_H
#define DAWN_PLAYER_FLV_MEDIA_STREAM_SOURCE_H

#include <memory>
#include <optional>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Storage.Streams.h>

#include "core/dawn_player/flv_player.hpp"
#include "core/dawn_player/io.hpp"
#include "core/dawn_player/task_service.hpp"

#include "FlvMediaStreamSource.g.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Core;
using namespace winrt::Windows::Storage::Streams;

using dawn_player::flv_player;
using dawn_player::io::read_stream_proxy;

namespace winrt::DawnPlayer::implementation
{
    struct FlvMediaStreamSource : FlvMediaStreamSourceT<FlvMediaStreamSource>
    {
        FlvMediaStreamSource(const std::shared_ptr<flv_player>& player, MediaStreamSource mss);
        static IAsyncOperation<DawnPlayer::FlvMediaStreamSource> CreateFromInputStreamAsync(IInputStream inputStream);
        static IAsyncOperation<DawnPlayer::FlvMediaStreamSource> CreateFromRandomAccessStreamAsync(IRandomAccessStream randomAccessStream);
        MediaStreamSource Source();

        virtual ~FlvMediaStreamSource();
        // IClosable
        void Close();
    private:
        std::shared_ptr<flv_player> player_;
        MediaStreamSource mss_ = nullptr;
        static IAsyncOperation<DawnPlayer::FlvMediaStreamSource> create_from_read_stream_proxy_async(std::shared_ptr<read_stream_proxy> stream_proxy);
        void on_starting(MediaStreamSource sender, MediaStreamSourceStartingEventArgs args);
        void on_sample_requested(MediaStreamSource sender, MediaStreamSourceSampleRequestedEventArgs args);
        void handle_starting(MediaStreamSource sender, MediaStreamSourceStartingEventArgs args);
        void handle_sample_requested(MediaStreamSource sender, MediaStreamSourceSampleRequestedEventArgs args);
        std::optional<winrt::event_token> starting_event_token;
        std::optional<winrt::event_token> sample_requested_event_token;
    };
}

namespace winrt::DawnPlayer::factory_implementation
{
    struct FlvMediaStreamSource : FlvMediaStreamSourceT<FlvMediaStreamSource, implementation::FlvMediaStreamSource>
    {
    };
}

#endif