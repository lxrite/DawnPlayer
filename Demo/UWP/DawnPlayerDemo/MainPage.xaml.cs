using System;
using Windows.Storage.Pickers;
using Windows.Storage.Streams;
using Windows.UI.Popups;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using DawnPlayer;

namespace DawnPlayerDemo
{
    public sealed partial class MainPage : Page
    {
        private uint playTaskID = 0;
        private FlvMediaStreamSource flvMediaStreamSource;
        private IInputStream videoStream;

        public MainPage()
        {
            this.InitializeComponent();
            this.Loaded += MainPage_Loaded;
            this.Unloaded += MainPage_Unloaded;
        }

        private void MainPage_Unloaded(object sender, RoutedEventArgs e)
        {
            if (this.mediaElement != null)
            {
                this.mediaElement.Source = null;
            }
            if (this.flvMediaStreamSource != null)
            {
                this.flvMediaStreamSource.Dispose();
                this.flvMediaStreamSource = null;
            }
            if (this.videoStream != null)
            {
                this.videoStream.Dispose();
                this.videoStream = null;
            }
        }

        private void MainPage_Loaded(object sender, RoutedEventArgs e)
        {
            this.Splitter.IsPaneOpen = true;
        }

        private async void OpenLocalFile(object sender, RoutedEventArgs e)
        {
            this.Splitter.IsPaneOpen = false;
            FileOpenPicker fileOpenPicker = new FileOpenPicker();
            fileOpenPicker.ViewMode = PickerViewMode.List;
            fileOpenPicker.SuggestedStartLocation = PickerLocationId.VideosLibrary;
            fileOpenPicker.FileTypeFilter.Add(".flv");
            var file = await fileOpenPicker.PickSingleFileAsync();
            if (file == null)
            {
                return;
            }
            this.mediaElement.Source = null;
            if (this.flvMediaStreamSource != null)
            {
                this.flvMediaStreamSource.Dispose();
                this.flvMediaStreamSource = null;
            }
            if (this.videoStream != null)
            {
                this.videoStream.Dispose();
                this.videoStream = null;
            }
            var taskID = ++this.playTaskID;
            IRandomAccessStream fileStream = null;
            try
            {
                fileStream = await file.OpenAsync(Windows.Storage.FileAccessMode.Read);
            }
            catch (Exception)
            {
            }
            if (taskID != this.playTaskID)
            {
                if (fileStream != null)
                {
                    fileStream.Dispose();
                    fileStream = null;
                }
                return;
            }
            if (fileStream == null)
            {
                this.ShowMessage("Failed to open media");
                return;
            }
            FlvMediaStreamSource fmss = null;
            try
            {
                fmss = await FlvMediaStreamSource.CreateFromRandomAccessStreamAsync(fileStream);
            }
            catch (Exception)
            {
                fileStream.Dispose();
                fileStream = null;
            }
            if (taskID != this.playTaskID)
            {
                if (fmss != null)
                {
                    fmss.Dispose();
                    fmss = null;
                }
                if (fileStream != null)
                {
                    fileStream.Dispose();
                    fileStream = null;
                }
                return;
            }
            if (fmss == null)
            {
                this.ShowMessage("Failed to open media");
                return;
            }
            this.videoStream = fileStream;
            this.flvMediaStreamSource = fmss;
            mediaElement.SetMediaStreamSource(flvMediaStreamSource.Source);
        }

        private void OnMediaEnd(object sender, RoutedEventArgs e)
        {
            this.mediaElement.Source = null;
            if (this.flvMediaStreamSource != null)
            {
                this.flvMediaStreamSource.Dispose();
                this.flvMediaStreamSource = null;
            }
            if (this.videoStream != null)
            {
                this.videoStream.Dispose();
                this.videoStream = null;
            }
            this.ShowMessage("OnMediaEnd");
        }

        private void OnMediaFailed(object sender, ExceptionRoutedEventArgs e)
        {
            this.mediaElement.Source = null;
            if (this.flvMediaStreamSource != null)
            {
                this.flvMediaStreamSource.Dispose();
                this.flvMediaStreamSource = null;
            }
            if (this.videoStream != null)
            {
                this.videoStream.Dispose();
                this.videoStream = null;
            }
            this.ShowMessage("OnMediaFailed");
        }

        private async void ShowMessage(string message)
        {
            var dialog = new MessageDialog(message);
            await dialog.ShowAsync();
        }
    }
}
