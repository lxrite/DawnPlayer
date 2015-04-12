/*
*    http_random_access_stream.hpp:
*
*    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
*
*/

#ifndef DAWN_PLAYER_HTTP_RANDOM_ACCESS_STREAM_HPP
#define DAWN_PLAYER_HTTP_RANDOM_ACCESS_STREAM_HPP

using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Web::Http;

namespace dawn_player {
namespace streams {

public ref class http_random_access_stream sealed : IRandomAccessStream
{
    Uri^ uri;
    HttpClient^ httpClient;
    IInputStream^ inputStream;
    bool seeked;
    unsigned long long seekPos;
    http_random_access_stream(Uri^ uri, HttpClient^ httpClient, IInputStream^ inputStream);
public:
    virtual ~http_random_access_stream();
    static IAsyncOperation<http_random_access_stream^>^ CreateFromUri(Uri^ uri);
    virtual IAsyncOperationWithProgress<IBuffer^, uint32>^ ReadAsync(IBuffer^ buffer, unsigned int count, InputStreamOptions options);
    virtual IAsyncOperationWithProgress<uint32, uint32>^ WriteAsync(IBuffer^ buffer);
    virtual IAsyncOperation<bool>^ FlushAsync();
    property bool CanRead {
        virtual bool get();
    }
    property bool CanWrite {
        virtual bool get();
    }
    property unsigned long long Position {
        virtual unsigned long long get();
    }
    property unsigned long long Size {
        virtual unsigned long long get();
        virtual void set(unsigned long long value);
    }
    virtual IRandomAccessStream^ CloneStream();
    virtual IInputStream^ GetInputStreamAt(unsigned long long position);
    virtual IOutputStream^ GetOutputStreamAt(unsigned long long position);
    virtual void Seek(unsigned long long position);
};

} // namespace streams
} // namespace dawn_player

#endif
