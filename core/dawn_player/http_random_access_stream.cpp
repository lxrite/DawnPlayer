/*
*    http_random_access_stream.cpp:
*
*    Copyright (C) 2015-2017 Light Lin <blog.poxiao.me> All Rights Reserved.
*
*/

#include <string>

#include <ppltasks.h>

#include "http_random_access_stream.hpp"

namespace dawn_player {
namespace streams {

http_random_access_stream::http_random_access_stream(Uri^ uri, HttpClient^ httpClient, IInputStream^ inputStream) :
    uri(uri),
    httpClient(httpClient),
    inputStream(inputStream),
    seeked(false),
    seekPos(0)
{
}

http_random_access_stream::~http_random_access_stream()
{
    this->uri = nullptr;
    this->httpClient = nullptr;
    this->inputStream = nullptr;
    this->seeked = false;
}

IAsyncOperation<http_random_access_stream^>^ http_random_access_stream::CreateFromUri(Uri^ uri)
{
    return concurrency::create_async([uri](concurrency::cancellation_token ct)->http_random_access_stream^ {
        HttpClient^ httpClient = ref new HttpClient();
        return concurrency::create_task(httpClient->GetInputStreamAsync(uri)).then([=](concurrency::task<IInputStream^> task) mutable {
            auto inputStream = task.get();
            return ref new http_random_access_stream(uri, httpClient, inputStream);
        }, ct).get();
    });
}

IAsyncOperationWithProgress<IBuffer^, uint32>^ http_random_access_stream::ReadAsync(IBuffer^ buffer, unsigned int count, InputStreamOptions options)
{
    if (!this->seeked && this->inputStream != nullptr) {
        return this->inputStream->ReadAsync(buffer, count, options);
    }
    else {
        auto is_seeked = this->seeked;
        auto position = this->seekPos;
        this->seeked = false;
        this->httpClient = nullptr;
        this->inputStream = nullptr;
        return concurrency::create_async([=](concurrency::progress_reporter<uint32>, concurrency::cancellation_token ct) -> IBuffer^ {
            this->httpClient = ref new HttpClient();
            if (is_seeked) {
                this->httpClient->DefaultRequestHeaders->Insert(ref new Platform::String(L"Range"), ref new Platform::String((L"bytes=" + std::to_wstring(position) + L"-").c_str()));
            }
            this->inputStream = concurrency::create_task(this->httpClient->GetInputStreamAsync(this->uri), ct).get();
            return concurrency::create_task(this->inputStream->ReadAsync(buffer, count, options), ct).get();
        });
    }
}

IAsyncOperationWithProgress<uint32, uint32>^ http_random_access_stream::WriteAsync(IBuffer^ buffer)
{
    throw ref new Platform::NotImplementedException();
}

IAsyncOperation<bool>^ http_random_access_stream::FlushAsync()
{
    throw ref new Platform::NotImplementedException();
}

bool http_random_access_stream::CanRead::get() {
    throw ref new Platform::NotImplementedException();
}

bool http_random_access_stream::CanWrite::get() {
    throw ref new Platform::NotImplementedException();
}

unsigned long long http_random_access_stream::Position::get() {
    throw ref new Platform::NotImplementedException();
}

unsigned long long http_random_access_stream::Size::get() {
    throw ref new Platform::NotImplementedException();
}

void http_random_access_stream::Size::set(unsigned long long value)
{
    throw ref new Platform::NotImplementedException();
}

IRandomAccessStream^ http_random_access_stream::CloneStream()
{
    throw ref new Platform::NotImplementedException();
}

IInputStream^ http_random_access_stream::GetInputStreamAt(unsigned long long position)
{
    throw ref new Platform::NotImplementedException();
}

IOutputStream^ http_random_access_stream::GetOutputStreamAt(unsigned long long position)
{
    throw ref new Platform::NotImplementedException();
}

void http_random_access_stream::Seek(unsigned long long position)
{
    this->seeked = true;
    this->seekPos = position;
}

} // namespace streams
} // namespace dawn_player
