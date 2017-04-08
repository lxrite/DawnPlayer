/*
 *    MainPage.xaml.cs:
 *
 *    Copyright (C) 2015 limhiaoing <blog.poxiao.me> All Rights Reserved.
 *
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Diagnostics;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

using dawn_player;

namespace Demo
{
    public sealed partial class MainPage : Page
    {
        public MainPage()
        {
            this.InitializeComponent();

            this.NavigationCacheMode = NavigationCacheMode.Required;
        }

        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
        }

        private bool isOpening = false;
        private flv_media_stream_source fmss;

        private async void OnLoadButtonClick(object sender, RoutedEventArgs e)
        {
            if (fmss == null && !isOpening)
            {
                isOpening = true;
                var applicationFolder = Windows.ApplicationModel.Package.Current.InstalledLocation; ;
                var folders = await applicationFolder.GetFoldersAsync();
                var storageFile = await applicationFolder.GetFileAsync("Assets\\test.flv");
                var randomAccessStream = await storageFile.OpenReadAsync();
                fmss = await flv_media_stream_source.create(randomAccessStream);
                isOpening = false;
                mediaElement.SetMediaStreamSource(fmss.unwrap());
                mediaElement.Play();
            }
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
            if (curState == MediaElementState.Closed)
            {
                if (fmss != null)
                {
                    fmss.Dispose();
                    fmss = null;
                }
            }
        }
    }
}
