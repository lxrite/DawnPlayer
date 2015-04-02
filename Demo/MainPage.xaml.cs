/*
 *    MainPage.xaml.cs:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Navigation;
using Microsoft.Phone.Controls;
using Microsoft.Phone.Shell;
using System.Windows.Media;
using System.Diagnostics;
using Windows.Storage.Streams;
using System.Windows.Threading;

using DawnPlayer;

namespace Demo
{
    public partial class MainPage : PhoneApplicationPage
    {
        private bool isUserSeek = false;
        private IRandomAccessStream randomAccessStream;
        public MainPage()
        {
            InitializeComponent();
            var dispatcherTimer = new DispatcherTimer();
            dispatcherTimer.Tick += OnTimerEvent;
            dispatcherTimer.Interval = new TimeSpan(0, 0, 1);
            dispatcherTimer.Start();
        }

        private void OnTimerEvent(object sender, EventArgs e)
        {
            var position = mediaElement.Position;
            var duration = mediaElement.NaturalDuration.TimeSpan;
            timeText.Text = String.Format("{0}:{1:D2}:{2:D2}/{3}:{4:D2}:{5:D2}",
                position.Hours, position.Minutes, position.Seconds,
                duration.Hours, duration.Minutes, duration.Seconds);
            if (!isUserSeek)
            {
                seekBar.Value = position.TotalSeconds;
            }
        }

        private async void OnPlayPauseButtonClick(object sender, RoutedEventArgs e)
        {
            var curState = mediaElement.CurrentState;
            if (curState == MediaElementState.Closed)
            {
                var applicationFolder = Windows.ApplicationModel.Package.Current.InstalledLocation;
                var storageFile = await applicationFolder.GetFileAsync("Assets\\test.flv");
                randomAccessStream = await storageFile.OpenReadAsync();
                mediaElement.SetSource(FlvMediaStreamSource.Wrap(randomAccessStream));
                mediaElement.Play();
            }
            else if (curState == MediaElementState.Playing)
            {
                mediaElement.Pause();
            }
            else if (curState == MediaElementState.Paused || curState == MediaElementState.Stopped)
            {
                mediaElement.Play();
            }
        }

        private void OnStopButtonClick(object sender, RoutedEventArgs e)
        {
            mediaElement.Stop();
        }

        private void OnCloseButtonClick(object sender, RoutedEventArgs e)
        {
            mediaElement.Source = null;
        }

        private void OnMediaOpened(object sender, RoutedEventArgs e)
        {
            Debug.WriteLine("OnMediaOpened");
            seekBar.Minimum = 0;
            seekBar.Maximum = mediaElement.NaturalDuration.TimeSpan.TotalSeconds;
        }

        private void OnMediaEnded(object sender, RoutedEventArgs e)
        {
            Debug.WriteLine("OnMediaEnded");
            mediaElement.Source = null;
        }

        private void OnMediaFailed(object sender, RoutedEventArgs e)
        {
            Debug.WriteLine("OnMediaFailed");
            mediaElement.Source = null;
        }

        private void OnCurrentStateChanged(object sender, RoutedEventArgs e)
        {
            var curState = (sender as MediaElement).CurrentState;
            Debug.WriteLine("OnCurrentStateChanged: " + curState.ToString());
            if (curState == MediaElementState.Playing)
            {
                playPauseButton.Content = "Pause";
            }
            else
            {
                playPauseButton.Content = "Play";
            }
            if (curState == MediaElementState.Closed)
            {
                if (randomAccessStream != null)
                {
                    randomAccessStream.Dispose();
                    randomAccessStream = null;
                }
            }
        }

        private void seekBarManipulationStarted(object sender, System.Windows.Input.ManipulationStartedEventArgs e)
        {
            isUserSeek = true;
        }

        private void seekBarManipulationCompleted(object sender, System.Windows.Input.ManipulationCompletedEventArgs e)
        {
            mediaElement.Position = TimeSpan.FromSeconds(seekBar.Value);
            isUserSeek = false;
        }

        private void volumeBarValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            const double c = 9.21045;
            if (mediaElement != null)
            {
                mediaElement.Volume = Math.Log(e.NewValue * (Math.Pow(Math.E, c) - 1) + 1) / c;
            }
        }
    }
}
