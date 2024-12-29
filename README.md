## DawnPlayer
A FLV playback library for Windows 10+ UWP and WinUI 3 Apps.

This library allows you to extend the functionality of `MediaPlayerElement` and `MediaElement` to support playing FLV video files.

![](https://raw.githubusercontent.com/lxrite/DawnPlayer/master/Demo/demo1.gif)

### Features
* No third-party dependencies
* HTTP FLV (live) stream and local file playback

### Requirements
* Microsoft Visual Studio 2022

### Usage
Download the DawnPlayer NuGet package here: https://www.nuget.org/packages/DawnPlayer/

XAML
``` xml
<MediaElement x:Name="mediaElement" AutoPlay="True"/>
```

C#
``` csharp
// Local file playback
StorageFile file = ...;
fileStream = await file.OpenAsync(Windows.Storage.FileAccessMode.Read);
flvMediaStreamSource = await FlvMediaStreamSource.CreateFromRandomAccessStreamAsync(fileStream);
mediaElement.SetMediaStreamSource(flvMediaStreamSource.Source);

// HTTP FLV playback
Uri uri = ...;
var httpClient = new HttpClient();
httpStream = await httpClient.GetInputStreamAsync(uri);
flvMediaStreamSource = await FlvMediaStreamSource.CreateFromInputStreamAsync(httpStream);
mediaElement.SetMediaStreamSource(flvMediaStreamSource.Source);
```
