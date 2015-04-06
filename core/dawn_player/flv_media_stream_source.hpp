/*
 *    flv_media_stream_source.hpp:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

#ifndef DAWN_PLAYER_FLV_MEDIA_STREAM_SOURCE_HPP
#define DAWN_PLAYER_FLV_MEDIA_STREAM_SOURCE_HPP

#include "flv_player.hpp"

using namespace Windows::Foundation;
using namespace Windows::Media::Core;
using namespace Windows::Storage::Streams;

namespace dawn_player {

public ref class flv_media_stream_source sealed {
private:
    flv_player^ player;
    MediaStreamSource^ mss;
private:
    flv_media_stream_source();
    void init(flv_player^ player, MediaStreamSource^ mss);
public:
    static IAsyncOperation<flv_media_stream_source^>^ create_async(IRandomAccessStream^ random_access_stream);
    MediaStreamSource^ unwrap();
private:
    void on_starting(MediaStreamSource^ sender, MediaStreamSourceStartingEventArgs^ args);
    void on_sample_requested(MediaStreamSource^ sender, MediaStreamSourceSampleRequestedEventArgs^ args);
    void on_closed(MediaStreamSource^ sender, MediaStreamSourceClosedEventArgs^ args);
    Windows::Foundation::EventRegistrationToken starting_event_token;
    Windows::Foundation::EventRegistrationToken sample_requested_event_token;
    Windows::Foundation::EventRegistrationToken closed_event_token;
};

} // namespace dawn_player

#endif
