## DawnPlayer
A FLV playback library for Windows Phone 8.1, Windows 8.1 UAP and Windows 10 UWP.

![](https://raw.githubusercontent.com/lxrite/DawnPlayer/master/Demo/demo1.gif)

### Features
* No third-party dependencies
* HTTP FLV (live) stream and local file playback
* Support Windows Phone 8.1, Windows 8.1 UAP and Windows 10 UWP

### Requirements
* Microsoft Visual Studio 2015

### Usage

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
