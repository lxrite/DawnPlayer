## DawnPlayer
A FLV playback library for Windows Phone Silverlight & Windows Runtime.

### Features
* No third-party dependencies
* Support both Windows Phone Silverlight & Windows Runtime

### Requirement
#### Windows Phone Silverlight
* Microsoft Visual Studio 2012
* Windows Phone 8.0 SDK

#### Windows Runtime
* Microsoft Visual studio 2013
* Windows (Phone) 8.1 SDK

### Usage

Asuming we have a `MediaElement` control named `mediaElement`, and there is a FLV file at `Assets\test.flv`.

#### Windows Phone Silverlight
``` csharp
var applicationFolder = Windows.ApplicationModel.Package.Current.InstalledLocation;
var storageFile = await applicationFolder.GetFileAsync("Assets\\test.flv");
var randomAccessStream = await storageFile.OpenReadAsync();
mediaElement.SetSource(FlvMediaStreamSource.Wrap(randomAccessStream));
mediaElement.Play();
```

#### Windows Runtime
``` csharp
var applicationFolder = Windows.ApplicationModel.Package.Current.InstalledLocation;
var folders = await applicationFolder.GetFoldersAsync();
var storageFile = await applicationFolder.GetFileAsync("Assets\\test.flv");
var randomAccessStream = await storageFile.OpenReadAsync();
fmss = await flv_media_stream_source.create_async(randomAccessStream);
mediaElement.SetMediaStreamSource(fmss.unwrap());
mediaElement.Play();
```

For HTTP or other streams, just implement the `IRandomAccessStream` interface around it. Here is a simple [http_random_access_stream](https://github.com/lxrite/DawnPlayer/blob/master/core/dawn_player/http_random_access_stream.hpp) for Windows Runtime.
