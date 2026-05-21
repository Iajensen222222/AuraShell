using AuraConfig.Services;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace AuraConfig.Pages;

public sealed partial class DesktopItemsPage : Page
{
    private string _targetPath = string.Empty;  // .lnk shortcut to modify
    private string _iconSource = string.Empty;  // .ico/.exe/.dll icon source

    public DesktopItemsPage() => InitializeComponent();

    // ── Drop zone (TARGET shortcut) ────────────────────────────────────────

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
            {
                var path = items[0].Path;
                if (path.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase))
                    SetTarget(path);
                else
                    ShowResult("Drop a .lnk shortcut file.", success: false);
            }
        }
    }

    // ── Browse shortcut (TARGET) ───────────────────────────────────────────

    private async void BrowseShortcut_Click(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();
        picker.FileTypeFilter.Add(".lnk");

        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(
            ((Application.Current as App)!.MainWindow));
        WinRT.Interop.InitializeWithWindow.Initialize(picker, hwnd);

        var file = await picker.PickSingleFileAsync();
        if (file is not null)
            SetTarget(file.Path);
    }

    private void SetTarget(string path)
    {
        _targetPath = path;
        SelectedPathText.Text = path;
        CurrentItemCard.Visibility = Visibility.Visible;
        ResetIconSource();
    }

    // ── Browse icon (SOURCE) ───────────────────────────────────────────────

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
            SetIconSource(file.Path);
    }

    private void SetIconSource(string path)
    {
        _iconSource = path;
        IconSourceText.Text = System.IO.Path.GetFileName(path);
        ApplyBtn.IsEnabled = true;
        HideResult();
    }

    private void ResetIconSource()
    {
        _iconSource = string.Empty;
        IconSourceText.Text = "No icon selected";
        ApplyBtn.IsEnabled = false;
        HideResult();
    }

    // ── Apply / Restore ────────────────────────────────────────────────────

    private void ApplyIcon_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_targetPath) || string.IsNullOrEmpty(_iconSource))
            return;

        bool ok = ShellShortcut.SetIcon(_targetPath, _iconSource, iconIndex: 0);
        ShowResult(
            ok ? "Icon applied. You may need to press F5 to refresh your desktop."
               : "Could not apply icon — check that the shortcut is not read-only.",
            success: ok);
    }

    private void RestoreDefault_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_targetPath)) return;

        bool ok = ShellShortcut.ClearIcon(_targetPath);
        ShowResult(
            ok ? "Default icon restored."
               : "Could not restore icon — check that the shortcut is not read-only.",
            success: ok);

        if (ok) ResetIconSource();
    }

    // ── Result banner ──────────────────────────────────────────────────────

    private void ShowResult(string message, bool success)
    {
        ResultText.Text = message;
        ResultText.Foreground = new SolidColorBrush(
            success ? Colors.LightGreen : Colors.OrangeRed);
        ResultText.Visibility = Visibility.Visible;
    }

    private void HideResult() => ResultText.Visibility = Visibility.Collapsed;
}
