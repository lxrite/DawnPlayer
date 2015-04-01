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

using DawnPlayer;

namespace Demo
{
    public partial class MainPage : PhoneApplicationPage
    {
        private IRandomAccessStream randomAccessStream;
        public MainPage()
        {
            InitializeComponent();
        }

        private async void OnPlayPauseButtonClick(object sender, RoutedEventArgs e)
        {
            var curState = mediaElement.CurrentState;
            if (curState == MediaElementState.Closed)
            {
                try
                {
                    var applicationFolder = Windows.ApplicationModel.Package.Current.InstalledLocation;
                    var storageFile = await applicationFolder.GetFileAsync("Assets\\test.flv");
                    randomAccessStream = await storageFile.OpenReadAsync();
                    mediaElement.SetSource(FlvMediaStreamSource.Wrap(randomAccessStream));
                    mediaElement.Play();
                }
                catch (Exception except)
                {
                    Debug.WriteLine("Exception: " + except.Message);
                }
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
        }

        private void OnMediaEnded(object sender, RoutedEventArgs e)
        {
            Debug.WriteLine("OnMediaEnded");
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
    }
}
