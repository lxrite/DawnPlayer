/*
 *    FlvMediaStreamSource.cs:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Windows.Media;
using Windows.Storage.Streams;

using dawn_player;

namespace DawnPlayer
{
    public class FlvMediaStreamSource : MediaStreamSource, IDisposable
    {
        private flv_player flvPlayer;
        private IRandomAccessStream randomAccessStream;
        private MediaStreamDescription audioStreamDescription;
        private MediaStreamDescription videoStreamDescription;
        private static Dictionary<MediaSampleAttributeKeys, string> emptySampleAttributes = new Dictionary<MediaSampleAttributeKeys, string>();
        private bool isClosed = false;
        private bool isErrorOcurred = false;
        private bool isSeeking = false;
        private bool disposed = false;

        public TimeSpan Position 
        {
            get
            {
                if (isClosed || isErrorOcurred)
                {
                    return TimeSpan.FromMilliseconds(0);
                }
                return TimeSpan.FromMilliseconds(flvPlayer.get_position() / 10000);
            }
        }

        private FlvMediaStreamSource(IRandomAccessStream ras)
        {
            randomAccessStream = ras;
        }

        public static FlvMediaStreamSource Wrap(IRandomAccessStream ras)
        {
            if (ras == null)
            {
                throw new ArgumentNullException("ArgumentNullException.");
            }
            return new FlvMediaStreamSource(ras);
        }

        protected override void CloseMedia()
        {
            flvPlayer.close();
            isClosed = true;
        }

        protected override void GetDiagnosticAsync(MediaStreamSourceDiagnosticKind diagnosticKind)
        {
            throw new NotImplementedException();
        }

        protected override async void GetSampleAsync(MediaStreamType mediaStreamType)
        {
            if (mediaStreamType == MediaStreamType.Script)
            {
                return;
            }
            sample_type sampleType = mediaStreamType == MediaStreamType.Audio ? sample_type.audio : sample_type.video;
            Dictionary<string, object> sampleInfo = new Dictionary<string, object>();
            var result = await flvPlayer.get_sample_async(sampleType, sampleInfo);
            if (isClosed || isErrorOcurred)
            {
                return;
            }
            if (result == get_sample_result.ok)
            {
                long timeStamp = Convert.ToInt64(sampleInfo["Timestamp"]);
                var sampleDataBuffer = sampleInfo["Data"] as Windows.Storage.Streams.IBuffer;
                var sampleStream = sampleDataBuffer.AsStream();
                if (mediaStreamType == MediaStreamType.Audio)
                {
                    ReportGetSampleCompleted(new MediaStreamSample(audioStreamDescription, sampleStream, 0, (long)sampleDataBuffer.Length, timeStamp, emptySampleAttributes));
                }
                else
                {
                    ReportGetSampleCompleted(new MediaStreamSample(videoStreamDescription, sampleStream, 0, (long)sampleDataBuffer.Length, timeStamp, emptySampleAttributes));
                }
            }
            else if (result == get_sample_result.eos)
            {
                if (mediaStreamType == MediaStreamType.Audio)
                {
                    ReportGetSampleCompleted(new MediaStreamSample(audioStreamDescription, null, 0, 0, 0, emptySampleAttributes));
                }
                else
                {
                    ReportGetSampleCompleted(new MediaStreamSample(videoStreamDescription, null, 0, 0, 0, emptySampleAttributes));
                }
            }
            else if (result == get_sample_result.error)
            {
                isErrorOcurred = true;
                ErrorOccurred("An error occured while parsing FLV file body.");
            }
        }

        protected override async void OpenMediaAsync()
        {
            flvPlayer = new flv_player();
            flvPlayer.set_source(randomAccessStream);
            var mediaInfo = new Dictionary<string, string>();
            var open_result = await flvPlayer.open_async(mediaInfo);
            if (isClosed)
            {
                return;
            }
            if (open_result == open_result.ok)
            {
                var mediaStreamAttributes = new Dictionary<MediaSourceAttributesKeys, string>();
                mediaStreamAttributes[MediaSourceAttributesKeys.Duration] = mediaInfo["Duration"];
                mediaStreamAttributes[MediaSourceAttributesKeys.CanSeek] = mediaInfo["CanSeek"];

                var audioStreamAttributes = new Dictionary<MediaStreamAttributeKeys, string>();
                audioStreamAttributes[MediaStreamAttributeKeys.CodecPrivateData] = mediaInfo["AudioCodecPrivateData"];

                var videoStreamAttributes = new Dictionary<MediaStreamAttributeKeys, string>();
                videoStreamAttributes[MediaStreamAttributeKeys.Height] = mediaInfo["Height"];
                videoStreamAttributes[MediaStreamAttributeKeys.Width] = mediaInfo["Width"];
                videoStreamAttributes[MediaStreamAttributeKeys.VideoFourCC] = mediaInfo["VideoFourCC"];
                videoStreamAttributes[MediaStreamAttributeKeys.CodecPrivateData] = mediaInfo["VideoCodecPrivateData"];

                var availableMediaStreams = new List<MediaStreamDescription>();
                audioStreamDescription = new MediaStreamDescription(MediaStreamType.Audio, audioStreamAttributes);
                availableMediaStreams.Add(audioStreamDescription);
                videoStreamDescription = new MediaStreamDescription(MediaStreamType.Video, videoStreamAttributes);
                availableMediaStreams.Add(videoStreamDescription);

                AudioBufferLength = 15;
                ReportOpenMediaCompleted(mediaStreamAttributes, availableMediaStreams);
            }
            else if (open_result == open_result.error)
            {
                isErrorOcurred = true;
                ErrorOccurred("Failed to open flv file.");
            }
        }

        protected override async void SeekAsync(long seekToTime)
        {
            if (isSeeking)
            {
                return;
            }
            isSeeking = true;
            var seekTo = await flvPlayer.begin_seek(seekToTime);
            if (isErrorOcurred || isClosed || seekTo == -1)
            {
                return;
            }
            ReportSeekCompleted(seekTo);
            flvPlayer.end_seek();
            isSeeking = false;
        }

        protected override void SwitchMediaStreamAsync(MediaStreamDescription mediaStreamDescription)
        {
            throw new NotImplementedException();
        }

        protected virtual void Dispose(bool disposing)
        {
            if (disposed)
            {
                return;
            }
            if (disposing)
            {
                flvPlayer.Dispose();
                randomAccessStream.Dispose();
            }
            flvPlayer = null;
            randomAccessStream = null;
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        ~FlvMediaStreamSource()
        {
            Dispose(false);
        }
    }
}
