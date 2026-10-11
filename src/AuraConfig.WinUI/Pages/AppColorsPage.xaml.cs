using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.UI;

namespace AuraConfig.Pages;

public sealed partial class AppColorsPage : Page
{
    private static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "app-colors.json");

    private readonly List<AppColorEntry> _entries = new();
    private string _selectedExe = "";

    private GlowAnimator? Animator =>
        (Application.Current as App)?.MainWindow?.GlowAnimator;

    public AppColorsPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Logger.Info("AppColorsPage", "Loaded");
        BuildPresetSwatches();
        LoadEntries();
        Logger.Debug("AppColorsPage", $"{_entries.Count} entries restored");
        RebuildList();
        ApplyAllToAnimator();
    }

    private void BuildPresetSwatches()
    {
        PresetSwatches.Items.Clear();
        foreach (var preset in GlowPresets.All)
        {
            // SubtleButtonStyle keeps the swatch color on hover (the default button
            // template would swap it for the gray hover fill).
            var btn = new Button
            {
                Style        = (Style)Application.Current.Resources["SubtleButtonStyle"],
                Width        = 30,
                Height       = 30,
                Padding      = new Thickness(0),
                CornerRadius = new CornerRadius(4),
                Background   = new SolidColorBrush(preset.Color),
                Tag          = preset,
            };
            ToolTipService.SetToolTip(btn, $"{preset.Name}  {preset.HexColor}");
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(btn, preset.Name);
            btn.Click += Swatch_Click;
            PresetSwatches.Items.Add(btn);
        }
    }

    // ── Persistence ────────────────────────────────────────────────────────

    private void LoadEntries()
    {
        _entries.Clear();
        try
        {
            if (!File.Exists(FilePath)) return;
            var list = JsonSerializer.Deserialize<List<AppColorEntry>>(File.ReadAllText(FilePath));
            if (list != null) _entries.AddRange(list);
        }
        catch { }
    }

    private void SaveEntries()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath,
                JsonSerializer.Serialize(_entries, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { }
    }

    private void ApplyAllToAnimator()
    {
        if (Animator is null) return;
        foreach (var entry in _entries)
            Animator.SetAppColor(entry.ExeName, entry.R, entry.G, entry.B);
    }

    // ── List rendering ─────────────────────────────────────────────────────

    private void RebuildList()
    {
        AppColorList.Children.Clear();
        EmptyState.Visibility = _entries.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        foreach (var entry in _entries)
            AppColorList.Children.Add(MakeRowCard(entry));
    }

    private Border MakeRowCard(AppColorEntry entry)
    {
        var swatch = new Border
        {
            Width             = 24,
            Height            = 24,
            CornerRadius      = new CornerRadius(4),
            Background        = new SolidColorBrush(entry.Color),
            VerticalAlignment = VerticalAlignment.Center,
            Margin            = new Thickness(0, 0, 16, 0),
        };

        var nameLabel = new TextBlock
        {
            Text  = entry.ExeName,
            Style = (Style)Application.Current.Resources["SettingTitleTextStyle"],
        };

        var hexLabel = new TextBlock
        {
            Text  = entry.HexColor,
            Style = (Style)Application.Current.Resources["SettingDescriptionTextStyle"],
        };

        var info = new StackPanel { VerticalAlignment = VerticalAlignment.Center };
        info.Children.Add(nameLabel);
        info.Children.Add(hexLabel);

        var editBtn = MakeIconButton("", $"Change color for {entry.ExeName}");
        editBtn.Click += (_, _) => StartEditing(entry.ExeName);

        var removeBtn = MakeIconButton("", $"Remove {entry.ExeName}");
        removeBtn.Click += (_, _) => RemoveEntry(entry.ExeName);

        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        actions.Children.Add(editBtn);
        actions.Children.Add(removeBtn);

        var rowGrid = new Grid();
        rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn(info, 1);
        Grid.SetColumn(actions, 2);
        rowGrid.Children.Add(swatch);
        rowGrid.Children.Add(info);
        rowGrid.Children.Add(actions);

        return new Border
        {
            Style   = (Style)Application.Current.Resources["CardStyle"],
            Padding = new Thickness(16, 10, 12, 10),
            Child   = rowGrid,
        };
    }

    private static Button MakeIconButton(string glyph, string label)
    {
        var btn = new Button
        {
            Style   = (Style)Application.Current.Resources["SubtleButtonStyle"],
            Content = new FontIcon { Glyph = glyph, FontSize = 14 },
            Padding = new Thickness(8),
        };
        ToolTipService.SetToolTip(btn, label);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(btn, label);
        return btn;
    }

    // ── Selection / editing ────────────────────────────────────────────────

    private void StartEditing(string exeName)
    {
        _selectedExe = exeName;
        SelectedExeText.Text = exeName;
        var existing = _entries.FirstOrDefault(e => e.ExeName == exeName);
        if (existing != null)
        {
            AppHexInput.Text = existing.HexColor;
            AppColorPreview.Background = new SolidColorBrush(existing.Color);
        }
        ColorPickerCard.Visibility = Visibility.Visible;
    }

    private void RemoveEntry(string exeName)
    {
        _entries.RemoveAll(e => e.ExeName == exeName);
        Animator?.ClearAppColor(exeName);
        SaveEntries();
        RebuildList();
        ShowStatus($"Removed {exeName}. It uses your theme color again.");
    }

    private void SetFromPath(string fullPath)
    {
        _selectedExe = Path.GetFileName(fullPath);
        SelectedExeText.Text = _selectedExe;
        AppHexInput.Text = "#0078D4";
        AppColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, 0x00, 0x78, 0xD4));
        ColorPickerCard.Visibility = Visibility.Visible;
    }

    // ── Color picker handlers ──────────────────────────────────────────────

    private void Swatch_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: GlowPreset preset }) return;
        AppHexInput.Text = preset.HexColor;
        AppColorPreview.Background = new SolidColorBrush(preset.Color);
    }

    private void AppHexInput_KeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter)
            ApplyHex_Click(sender, null!);
    }

    private void AppColorPreview_Click(object sender, RoutedEventArgs e)
    {
        var c = (AppColorPreview.Background as SolidColorBrush)?.Color
                ?? Color.FromArgb(255, 0x00, 0x78, 0xD4);
        DashboardPage.ShowColorPickerWindow(c.R, c.G, c.B, (nr, ng, nb) =>
        {
            AppHexInput.Text = $"#{nr:X2}{ng:X2}{nb:X2}";
            AppColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, nr, ng, nb));
        });
    }

    private void CancelOverride_Click(object sender, RoutedEventArgs e)
    {
        _selectedExe = "";
        ColorPickerCard.Visibility = Visibility.Collapsed;
    }

    private void ApplyHex_Click(object sender, RoutedEventArgs e)
    {
        var hex = AppHexInput.Text.Trim().TrimStart('#');
        if (hex.Length != 6) return;
        try
        {
            byte r = Convert.ToByte(hex[0..2], 16);
            byte g = Convert.ToByte(hex[2..4], 16);
            byte b = Convert.ToByte(hex[4..6], 16);
            AppColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));
        }
        catch { }
    }

    private void SaveOverride_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_selectedExe)) return;
        var hex = AppHexInput.Text.Trim().TrimStart('#');
        if (hex.Length != 6) return;
        try
        {
            byte r = Convert.ToByte(hex[0..2], 16);
            byte g = Convert.ToByte(hex[2..4], 16);
            byte b = Convert.ToByte(hex[4..6], 16);

            var existing = _entries.FirstOrDefault(e => e.ExeName == _selectedExe);
            if (existing != null)
            {
                existing.R = r; existing.G = g; existing.B = b;
            }
            else
            {
                _entries.Add(new AppColorEntry
                {
                    ExeName = _selectedExe, R = r, G = g, B = b,
                });
            }

            Animator?.SetAppColor(_selectedExe, r, g, b);
            SaveEntries();
            RebuildList();
            Logger.Info("AppColorsPage", $"Saved override {_selectedExe} → #{hex.ToUpperInvariant()}");
            ShowStatus($"Saved: {_selectedExe} now glows #{hex.ToUpperInvariant()}");
            ColorPickerCard.Visibility = Visibility.Collapsed;
        }
        catch (Exception ex) { Logger.Error("AppColorsPage", "Save override failed", ex); }
    }

    // ── Drop zone ──────────────────────────────────────────────────────────

    private void DropZone_DragEnter(object sender, DragEventArgs e)
    {
        e.AcceptedOperation = DataPackageOperation.Copy;
        ExeDropZone.BorderBrush =
            (Brush)Application.Current.Resources["AccentTextFillColorPrimaryBrush"];
    }

    private void DropZone_DragLeave(object sender, DragEventArgs e)
        => ExeDropZone.BorderBrush =
            (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"];

    private async void DropZone_Drop(object sender, DragEventArgs e)
    {
        ExeDropZone.BorderBrush =
            (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"];
        if (e.DataView.Contains(StandardDataFormats.StorageItems))
        {
            var items = await e.DataView.GetStorageItemsAsync();
            if (items.Count > 0)
            {
                var path = items[0].Path;
                if (path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                    SetFromPath(path);
            }
        }
    }

    private async void BrowseApp_Click(object sender, RoutedEventArgs e)
    {
        var picker = new Windows.Storage.Pickers.FileOpenPicker();
        picker.FileTypeFilter.Add(".exe");
        var win = (Application.Current as App)?.MainWindow;
        if (win is null) return;
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(win);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, hwnd);
        var file = await picker.PickSingleFileAsync();
        if (file is not null) SetFromPath(file.Path);
    }

    // ── Status ─────────────────────────────────────────────────────────────

    private async void ShowStatus(string message)
    {
        StatusText.Text         = message;
        StatusBanner.Visibility = Visibility.Visible;
        await Task.Delay(2500);
        StatusBanner.Visibility = Visibility.Collapsed;
    }
}
