/*
 *    flv_media_stream_source.hpp:
 *
 *    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FLV_MEDIA_STREAM_SOURCE_HPP
#define DAWN_PLAYER_FLV_MEDIA_STREAM_SOURCE_HPP

#include <memory>

#include "flv_player.hpp"
#include "io.hpp"
#include "task_service.hpp"

using namespace Windows::Foundation;
using namespace Windows::Media::Core;
using namespace Windows::Storage::Streams;

using namespace dawn_player;
using namespace dawn_player::io;

namespace DawnPlayer {

public ref class FlvMediaStreamSource sealed {
private:
    std::shared_ptr<flv_player> player;
    MediaStreamSource^ mss;
private:
    FlvMediaStreamSource();
    void init(const std::shared_ptr<flv_player>& player, MediaStreamSource^ mss);
public:
    static IAsyncOperation<FlvMediaStreamSource^>^ CreateFromInputStreamAsync(IInputStream^ inputStream);
    static IAsyncOperation<FlvMediaStreamSource^>^ CreateFromRandomAccessStreamAsync(IRandomAccessStream^ randomAccessStream);
    property MediaStreamSource^ Source
    {
        MediaStreamSource^ get();
    }
    virtual ~FlvMediaStreamSource();
private:
    static IAsyncOperation<FlvMediaStreamSource^>^ create_from_read_stream_proxy_async(const std::shared_ptr<read_stream_proxy>& stream_proxy);
    void on_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args);
    void on_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args);
    std::future<void> handle_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args);
    std::future<void> handle_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args);
    Windows::Foundation::EventRegistrationToken starting_event_token;
    Windows::Foundation::EventRegistrationToken sample_requested_event_token;
};

} // namespace DawnPlayer

#endif
