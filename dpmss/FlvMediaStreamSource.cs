/*
 *    FlvMediaStreamSource.cs:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Windows.Media;
using System.Runtime.InteropServices.WindowsRuntime;

namespace DawnPlayer
{
    public class FlvMediaStreamSource : MediaStreamSource
    {
        private dawn_player.flv_player flvPlayer;
        MediaStreamDescription audioStreamDescription;
        MediaStreamDescription videoStreamDescription;
        Dictionary<MediaSampleAttributeKeys, string> emptySampleAttributes = new Dictionary<MediaSampleAttributeKeys, string>();
        
        public FlvMediaStreamSource()
        {
            flvPlayer = new dawn_player.flv_player();
            flvPlayer.open_media_completed_event += OnOpenMediaCompleted;
            flvPlayer.get_sample_competed_event += OnGetSampleCompleted;
            flvPlayer.seek_completed_event += OnSeekCompleted;
            flvPlayer.error_occured_event += OnErrorOcurred;
        }

        public void SetStream(Windows.Storage.Streams.IRandomAccessStream randomAccessStream)
        {
            flvPlayer.set_source(randomAccessStream);
        }

        protected override void CloseMedia()
        {
            flvPlayer.close();
        }

        protected override void GetDiagnosticAsync(MediaStreamSourceDiagnosticKind diagnosticKind)
        {
            throw new NotImplementedException();
        }

        protected override void GetSampleAsync(MediaStreamType mediaStreamType)
        {
            if (mediaStreamType == MediaStreamType.Script)
            {
                return;
            }
            dawn_player.sample_type sampleType = mediaStreamType == MediaStreamType.Audio ? dawn_player.sample_type.audio : dawn_player.sample_type.video;
            flvPlayer.get_sample_async(sampleType);
        }

        protected override void OpenMediaAsync()
        {
            flvPlayer.open_async();
        }

        protected override void SeekAsync(long seekToTime)
        {
            flvPlayer.seek_async(seekToTime);
        }

        protected override void SwitchMediaStreamAsync(MediaStreamDescription mediaStreamDescription)
        {
            throw new NotImplementedException();
        }

        private void OnOpenMediaCompleted(IDictionary<string, string> mediaInfo)
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

            this.AudioBufferLength = 15;
            ReportOpenMediaCompleted(mediaStreamAttributes, availableMediaStreams);
        }

        private void OnGetSampleCompleted(dawn_player.sample_type type, IDictionary<string, Object> sampleInfo)
        {
            if (sampleInfo != null)
            {
                long timeStamp = Convert.ToInt64(sampleInfo["Timestamp"]);
                var sampleDataBuffer = sampleInfo["Data"] as Windows.Storage.Streams.IBuffer;
                var sampleStream = sampleDataBuffer.AsStream();
                if (type == dawn_player.sample_type.audio)
                {
                    ReportGetSampleCompleted(new MediaStreamSample(this.audioStreamDescription, sampleStream, 0, (long)sampleDataBuffer.Length, timeStamp, this.emptySampleAttributes));
                }
                else
                {
                    ReportGetSampleCompleted(new MediaStreamSample(this.videoStreamDescription, sampleStream, 0, (long)sampleDataBuffer.Length, timeStamp, this.emptySampleAttributes));
                }
            }
            else
            {
                if (type == dawn_player.sample_type.audio)
                {
                    this.ReportGetSampleCompleted(new MediaStreamSample(this.audioStreamDescription, null, 0, 0, 0, this.emptySampleAttributes));
                }
                else
                {
                    this.ReportGetSampleCompleted(new MediaStreamSample(this.videoStreamDescription, null, 0, 0, 0, this.emptySampleAttributes));
                }
            }
        }

        private void OnSeekCompleted(long seekToTime)
        {
            ReportSeekCompleted(seekToTime);
        }

        private void OnErrorOcurred(string errorDescription)
        {
            ErrorOccurred(errorDescription);
        }
    }
}
