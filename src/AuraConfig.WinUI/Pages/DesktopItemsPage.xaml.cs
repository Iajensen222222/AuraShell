using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace AuraConfig.Pages;

public sealed partial class DesktopItemsPage : Page
{
    private string _selectedPath = string.Empty;

    public DesktopItemsPage() => InitializeComponent();

    // ── Drop zone ──────────────────────────────────────────────────────────

    private void DropZone_DragEnter(object sender, DragEventArgs e)
    {
        e.AcceptedOperation = Windows.ApplicationModel.DataTransfer.DataPackageOperation.Copy;
        DropZone.BorderBrush = (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];
    }

    private void DropZone_DragLeave(object sender, DragEventArgs e)
    {
        DropZone.BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"];
    }

    private async void DropZone_Drop(object sender, DragEventArgs e)
    {
        DropZone.BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"];

        if (e.DataView.Contains(Windows.ApplicationModel.DataTransfer.StandardDataFormats.StorageItems))
        {
            var items = await e.DataView.GetStorageItemsAsync();
            if (items.Count > 0)
                ShowSelected(items[0].Path);
        }
    }

    // ── Browse ─────────────────────────────────────────────────────────────

    private async void BrowseIcon_Click(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();
        picker.FileTypeFilter.Add(".ico");
        picker.FileTypeFilter.Add(".exe");
        picker.FileTypeFilter.Add(".dll");
        picker.FileTypeFilter.Add(".png");

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(
            ((Application.Current as App)!.MainWindow));
        WinRT.Interop.InitializeWithWindow.Initialize(picker, hwnd);

        var file = await picker.PickSingleFileAsync();
        if (file is not null)
            ShowSelected(file.Path);
    }

    // ── Apply / Restore ────────────────────────────────────────────────────

    private void ApplyIcon_Click(object sender, RoutedEventArgs e)
    {
        // v1.1 feature — placeholder stub
    }

    private void RestoreDefault_Click(object sender, RoutedEventArgs e)
    {
        // v1.1 feature — placeholder stub
    }

    // ── Helper ─────────────────────────────────────────────────────────────

    private void ShowSelected(string path)
    {
        _selectedPath = path;
        SelectedPathText.Text = path;
        CurrentItemCard.Visibility = Visibility.Visible;
    }
}
