using System.Collections.ObjectModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.Storage;
using Windows.Storage.Pickers;

namespace AuraConfig.Pages;

public sealed partial class DashboardPage : Page
{
    private DashboardItem? _lastApplied;

    // Accent orange matches the nav pill color
    private static readonly Color AccentOrange = Color.FromArgb(0xFF, 0xFF, 0x8C, 0x00);

    // Hotkey tracking: service registration id → item Guid
    private readonly Dictionary<int, Guid> _hotkeyIds = new();

    [DllImport("user32.dll")]
    private static extern short GetKeyState(int nVirtKey);

    public DashboardPage()
    {
        InitializeComponent();
        PresetsGrid.ContainerContentChanging += OnContainerContentChanging;
        Loaded   += OnLoaded;
        Unloaded += OnUnloaded;
    }

    // ── Container painting ─────────────────────────────────────────────────

    private void OnContainerContentChanging(ListViewBase sender,
                                             ContainerContentChangingEventArgs args)
    {
        if (args.Item is not DashboardItem item) return;
        if (args.ItemContainer.ContentTemplateRoot is not Border outerBorder) return;
        if (outerBorder.Child is not Grid grid) return;

        // Row 0 = preview, row 1 = info row
        if (grid.Children[0] is not Border preview) return;
        if (grid.Children[1] is not Grid infoRow) return;

        // Paint the preview background based on item type
        PaintPreview(preview, item);

        // Mode chip
        if (infoRow.Children[0] is Border chipBorder &&
            chipBorder.Child is TextBlock chipText)
        {
            bool hasLabel = !string.IsNullOrEmpty(item.ModeLabel);
            chipBorder.Visibility = hasLabel ? Visibility.Visible : Visibility.Collapsed;
            chipText.Text = item.ModeLabel;

            Color chipColor = item.Mode switch
            {
                BorderMode.Glow        => Color.FromArgb(0xFF, 0x00, 0x78, 0xD4),
                BorderMode.FadeBetween => Color.FromArgb(0xFF, 0xFF, 0x8C, 0x00),
                BorderMode.Rainbow     => Color.FromArgb(0xFF, 0x9B, 0x59, 0xB6),
                BorderMode.LocationGlow  => Color.FromArgb(0xFF, 0x80, 0x00, 0xFF),
                BorderMode.AudioReactive => Color.FromArgb(0xFF, 0x00, 0xC8, 0xFF),
                _                        => Colors.Gray,
            };
            chipBorder.Background = new SolidColorBrush(chipColor) { Opacity = 0.25 };
            chipText.Foreground   = new SolidColorBrush(chipColor);
        }

        // Description label — show only when the item has a description
        foreach (UIElement el in infoRow.Children)
        {
            if (el is TextBlock tb && tb.Tag is "desc")
            {
                tb.Visibility = string.IsNullOrEmpty(item.Description)
                    ? Visibility.Collapsed : Visibility.Visible;
                break;
            }
        }

        // Active border highlight
        bool isActive = item.Id == DashboardItemStore.Instance.LastAppliedId;
        outerBorder.BorderBrush = isActive
            ? new SolidColorBrush(AccentOrange)
            : new SolidColorBrush(Colors.Transparent);
    }

    private static void PaintPreview(Border preview, DashboardItem item)
    {
        switch (item.Mode)
        {
            case BorderMode.SolidColor:
            {
                var c = item.Color;
                var transparent = Color.FromArgb(0, c.R, c.G, c.B);
                preview.Background = new LinearGradientBrush
                {
                    StartPoint = new Windows.Foundation.Point(0.5, 0),
                    EndPoint   = new Windows.Foundation.Point(0.5, 1),
                    GradientStops =
                    {
                        new GradientStop { Color = transparent,           Offset = 0.0 },
                        new GradientStop { Color = c with { A = 50 },     Offset = 0.5 },
                        new GradientStop { Color = transparent,           Offset = 1.0 },
                    }
                };
                break;
            }

            case BorderMode.Glow:
            {
                var c = item.Color;
                var black = Color.FromArgb(255, 0, 0, 0);
                var transparent = Color.FromArgb(0, c.R, c.G, c.B);
                preview.Background = new LinearGradientBrush
                {
                    StartPoint = new Windows.Foundation.Point(0.5, 0),
                    EndPoint   = new Windows.Foundation.Point(0.5, 1),
                    GradientStops =
                    {
                        new GradientStop { Color = transparent,           Offset = 0.0 },
                        new GradientStop { Color = c with { A = 80 },     Offset = 0.5 },
                        new GradientStop { Color = transparent,           Offset = 1.0 },
                    }
                };
                break;
            }

            case BorderMode.FadeBetween:
            {
                var c1 = item.Color;
                var c2 = Color.FromArgb(255, item.R2, item.G2, item.B2);
                preview.Background = new LinearGradientBrush
                {
                    StartPoint = new Windows.Foundation.Point(0, 0.5),
                    EndPoint   = new Windows.Foundation.Point(1, 0.5),
                    GradientStops =
                    {
                        new GradientStop { Color = c1 with { A = 80 }, Offset = 0.0 },
                        new GradientStop { Color = c2 with { A = 80 }, Offset = 1.0 },
                    }
                };
                break;
            }

            case BorderMode.AudioReactive:
            {
                var c1 = item.Color;
                var c2 = Color.FromArgb(255, item.R2, item.G2, item.B2);
                preview.Background = new LinearGradientBrush
                {
                    StartPoint = new Windows.Foundation.Point(0, 0.5),
                    EndPoint   = new Windows.Foundation.Point(1, 0.5),
                    GradientStops =
                    {
                        new GradientStop { Color = c1 with { A = 110 }, Offset = 0.0 },
                        new GradientStop { Color = c2 with { A = 110 }, Offset = 1.0 },
                    }
                };
                break;
            }

            case BorderMode.LocationGlow:
            {
                var c1 = item.Color;
                var c2 = Color.FromArgb(255, item.R2, item.G2, item.B2);
                preview.Background = new LinearGradientBrush
                {
                    StartPoint = new Windows.Foundation.Point(0.5, 0),
                    EndPoint   = new Windows.Foundation.Point(0.5, 1),
                    GradientStops =
                    {
                        new GradientStop { Color = c1 with { A = 80 }, Offset = 0.0 },
                        new GradientStop { Color = c2 with { A = 80 }, Offset = 1.0 },
                    }
                };
                break;
            }

            case BorderMode.Rainbow:
            {
                var stops = new GradientStopCollection();
                var colors = item.RainbowColors;
                if (colors.Count == 0)
                {
                    preview.Background = new SolidColorBrush(Color.FromArgb(0xFF, 0x20, 0x20, 0x20));
                    break;
                }
                for (int i = 0; i < colors.Count; i++)
                {
                    stops.Add(new GradientStop
                    {
                        Color  = Color.FromArgb(0x80, colors[i].R, colors[i].G, colors[i].B),
                        Offset = (double)i / Math.Max(1, colors.Count - 1),
                    });
                }
                preview.Background = new LinearGradientBrush
                {
                    StartPoint = new Windows.Foundation.Point(0, 0.5),
                    EndPoint   = new Windows.Foundation.Point(1, 0.5),
                    GradientStops = stops,
                };
                break;
            }
        }
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Logger.Info("DashboardPage", "Loaded");
        ServiceManager.Instance.PerfStatsReceived += OnPerfStatsReceived;
        RefreshFilteredItems();

        // Restore last-applied mode
        if (DashboardItemStore.Instance.LastAppliedId is Guid lastId)
        {
            var item = DashboardItemStore.Instance.Items.FirstOrDefault(x => x.Id == lastId);
            if (item is not null)
                ApplyItem(item, showBanner: false);
        }

        RegisterAllHotkeys();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        ServiceManager.Instance.PerfStatsReceived -= OnPerfStatsReceived;
    }

    private void OnPerfStatsReceived(object? sender, PerfStatsPayload p)
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            CpuText.Text = $"CPU: {p.CpuPercent:F1}%";
            MemText.Text = $"MEM: {p.MemoryMB:F0} MB";
            FpsText.Text = $"FPS: {p.AvgFps:F0}";
        });
    }

    private void UpdateEmptyState()
    {
        RefreshFilteredItems();
    }

    // ── Search ─────────────────────────────────────────────────────────────

    private string _searchText = "";

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        _searchText = ((TextBox)sender).Text.Trim();
        RefreshFilteredItems();
    }

    private void RefreshFilteredItems()
    {
        var all = DashboardItemStore.Instance.Items;
        if (string.IsNullOrEmpty(_searchText))
        {
            // Bind directly to ObservableCollection so drag-to-reorder works natively
            PresetsGrid.ItemsSource     = all;
            PresetsGrid.CanReorderItems = true;
            bool empty = all.Count == 0;
            EmptyState.Visibility  = empty ? Visibility.Visible  : Visibility.Collapsed;
            PresetsGrid.Visibility = empty ? Visibility.Collapsed : Visibility.Visible;
        }
        else
        {
            var list = all
                .Where(i => i.Name.Contains(_searchText, StringComparison.OrdinalIgnoreCase) ||
                            i.Description.Contains(_searchText, StringComparison.OrdinalIgnoreCase))
                .ToList();
            PresetsGrid.ItemsSource     = list;
            PresetsGrid.CanReorderItems = false;   // block reorder on filtered subset
            bool empty = list.Count == 0;
            EmptyState.Visibility  = empty ? Visibility.Visible  : Visibility.Collapsed;
            PresetsGrid.Visibility = empty ? Visibility.Collapsed : Visibility.Visible;
        }
    }

    // ── Drag-to-reorder ────────────────────────────────────────────────────

    private void PresetsGrid_DragItemsCompleted(ListViewBase sender,
        DragItemsCompletedEventArgs args)
        => DashboardItemStore.Instance.SaveOrder();

    // ── Import / Export ────────────────────────────────────────────────────

    private IntPtr AppHwnd =>
        WinRT.Interop.WindowNative.GetWindowHandle(
            ((App)Application.Current).MainWindow!);

    private async void ExportPresets_Click(object sender, RoutedEventArgs e)
    {
        var picker = new FileSavePicker();
        WinRT.Interop.InitializeWithWindow.Initialize(picker, AppHwnd);
        picker.SuggestedFileName = "AuraShell_Presets";
        picker.FileTypeChoices.Add("AuraShell Presets", new List<string> { ".json" });

        var file = await picker.PickSaveFileAsync();
        if (file is null) return;

        var json = System.Text.Json.JsonSerializer.Serialize(
            DashboardItemStore.Instance.Items.ToList(),
            new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
        await FileIO.WriteTextAsync(file, json);
    }

    private async void ImportPresets_Click(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(picker, AppHwnd);
        picker.FileTypeFilter.Add(".json");

        var file = await picker.PickSingleFileAsync();
        if (file is null) return;
        try
        {
            var json  = await FileIO.ReadTextAsync(file);
            var items = System.Text.Json.JsonSerializer.Deserialize<List<DashboardItem>>(json);
            if (items is null) return;
            foreach (var item in items)
            {
                item.Id = Guid.NewGuid();
                DashboardItemStore.Instance.Add(item);
            }
            UpdateEmptyState();
        }
        catch { }
    }

    private async Task ExportSinglePresetAsync(DashboardItem item)
    {
        var picker = new FileSavePicker();
        WinRT.Interop.InitializeWithWindow.Initialize(picker, AppHwnd);
        picker.SuggestedFileName = item.Name.Replace(" ", "_");
        picker.FileTypeChoices.Add("AuraShell Preset", new List<string> { ".json" });

        var file = await picker.PickSaveFileAsync();
        if (file is null) return;

        var json = System.Text.Json.JsonSerializer.Serialize(
            new List<DashboardItem> { item },
            new System.Text.Json.JsonSerializerOptions { WriteIndented = true });
        await FileIO.WriteTextAsync(file, json);
    }

    private async Task ImportSinglePresetAsync()
    {
        var picker = new FileOpenPicker();
        WinRT.Interop.InitializeWithWindow.Initialize(picker, AppHwnd);
        picker.FileTypeFilter.Add(".json");

        var file = await picker.PickSingleFileAsync();
        if (file is null) return;
        try
        {
            var json  = await FileIO.ReadTextAsync(file);
            var items = System.Text.Json.JsonSerializer.Deserialize<List<DashboardItem>>(json);
            if (items is null || items.Count == 0) return;

            // Import only the first preset from the file
            var imported = items[0];
            imported.Id = Guid.NewGuid();
            DashboardItemStore.Instance.Add(imported);
            UpdateEmptyState();
        }
        catch { }
    }

    // ── Global hotkeys ─────────────────────────────────────────────────────

    private void RegisterAllHotkeys()
    {
        HotkeyService.Instance.UnregisterAll();
        _hotkeyIds.Clear();
        foreach (var item in DashboardItemStore.Instance.Items)
        {
            if (item.HotkeyKey == 0) continue;
            var captured = item;
            int id = HotkeyService.Instance.Register(
                item.HotkeyModifiers, item.HotkeyKey,
                () => ApplyItem(captured, showBanner: true));
            if (id >= 0) _hotkeyIds[id] = item.Id;
        }
    }

    // ── Apply item ─────────────────────────────────────────────────────────

    private void PresetsGrid_ItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is not DashboardItem item) return;
        ApplyItem(item, showBanner: true);
    }

    private void ApplyItem(DashboardItem item, bool showBanner)
    {
        _lastApplied = item;
        DashboardItemStore.Instance.SetApplied(item.Id);

        var anim = ((App)Application.Current).MainWindow?.GlowAnimator;
        if (anim is not null)
        {
            anim.SetBrightness(item.Brightness / 100f);
            switch (item.Mode)
            {
                case BorderMode.SolidColor:
                    anim.StopAnimationMode();
                    anim.SetColor(item.R, item.G, item.B);
                    _ = ServiceManager.Instance.ApplyPresetAsync(
                        new GlowPreset(item.Name, item.R, item.G, item.B, 100, 1.0));
                    break;

                case BorderMode.Glow:
                    anim.StartGlowMode(item.R, item.G, item.B,
                        item.FadeInSec, item.StayOnColorSec, item.FadeOutSec, item.StayOnBlackSec);
                    break;

                case BorderMode.FadeBetween:
                    anim.StartFadeBetweenMode(
                        item.R, item.G, item.B,
                        item.R2, item.G2, item.B2,
                        item.FadeInSec, item.StayOnColorSec, item.FadeOutSec, item.StayOnBlackSec);
                    break;

                case BorderMode.Rainbow:
                    var colors = item.RainbowColors
                        .Select(c => (c.R, c.G, c.B))
                        .ToList<(byte R, byte G, byte B)>();
                    if (colors.Count >= 2)
                        anim.StartRainbowMode(colors, item.TransitionSec);
                    break;

                case BorderMode.AudioReactive:
                    anim.StartAudioReactiveMode(
                        item.R,  item.G,  item.B,
                        item.R2, item.G2, item.B2,
                        item.AudioHueWeight);
                    break;

                case BorderMode.LocationGlow:
                    anim.StartLocationGlowMode(item.R, item.G, item.B, item.R2, item.G2, item.B2);
                    break;

            }
        }

        RefreshAllCardBorders();

        // Tint the titlebar to match the active preset's primary color
        var mainWin = ((App)Application.Current).MainWindow;
        if (mainWin is not null)
        {
            byte tr = item.R, tg = item.G, tb = item.B;
            if (item.Mode == BorderMode.Rainbow && item.RainbowColors.Count > 0)
            {
                tr = item.RainbowColors[0].R;
                tg = item.RainbowColors[0].G;
                tb = item.RainbowColors[0].B;
            }
            mainWin.SetTitleBarAccent(tr, tg, tb);
        }

        if (showBanner)
            ShowAppliedBanner(item.Name);
    }

    private void RefreshAllCardBorders()
    {
        var store = DashboardItemStore.Instance;
        foreach (var item in store.Items)
        {
            var container = PresetsGrid.ContainerFromItem(item) as GridViewItem;
            if (container?.ContentTemplateRoot is not Border outerBorder) continue;
            bool isActive = item.Id == store.LastAppliedId;
            outerBorder.BorderBrush = isActive
                ? new SolidColorBrush(AccentOrange)
                : new SolidColorBrush(Colors.Transparent);
        }
    }

    private async void ShowAppliedBanner(string name)
    {
        AppliedText.Text = $"✓  {name} applied";
        AppliedBanner.Visibility = Visibility.Visible;
        await Task.Delay(3000);
        if (_lastApplied?.Name == name)
            AppliedBanner.Visibility = Visibility.Collapsed;
    }

    // ── Add new item ───────────────────────────────────────────────────────

    private async void AddItemBtn_Click(object sender, RoutedEventArgs e)
    {
        var item = new DashboardItem { Name = "New Item" };
        DashboardItemStore.Instance.Add(item);
        UpdateEmptyState();
        await ShowItemDialog(item, isNew: true);
    }

    // ── Three-dots menu ────────────────────────────────────────────────────

    private void ThreeDotsBtn_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.Tag is not DashboardItem item) return;

        var flyout = new MenuFlyout();

        var editItem = new MenuFlyoutItem { Text = "Edit", Icon = new FontIcon { Glyph = "" } };
        editItem.Click += async (_, _) => await ShowItemDialog(item, isNew: false);

        var renameItem = new MenuFlyoutItem { Text = "Rename", Icon = new FontIcon { Glyph = "" } };
        renameItem.Click += async (_, _) => await ShowRenameDialog(item);

        var dupItem = new MenuFlyoutItem { Text = "Duplicate", Icon = new FontIcon { Glyph = "" } };
        dupItem.Click += (_, _) => { DashboardItemStore.Instance.Duplicate(item.Id); UpdateEmptyState(); };

        var sep = new MenuFlyoutSeparator();

        var delItem = new MenuFlyoutItem { Text = "Delete", Icon = new FontIcon { Glyph = "" } };
        delItem.Foreground = new SolidColorBrush(Color.FromArgb(0xFF, 0xFF, 0x60, 0x60));
        delItem.Click += async (_, _) => await DeleteItemAsync(item);

        var exportItem = new MenuFlyoutItem { Text = "Export Preset", Icon = new FontIcon { Glyph = "" } };
        exportItem.Click += async (_, _) => await ExportSinglePresetAsync(item);

        var importItem = new MenuFlyoutItem { Text = "Import Preset", Icon = new FontIcon { Glyph = "" } };
        importItem.Click += async (_, _) => await ImportSinglePresetAsync();

        flyout.Items.Add(editItem);
        flyout.Items.Add(renameItem);
        flyout.Items.Add(dupItem);
        flyout.Items.Add(new MenuFlyoutSeparator());
        flyout.Items.Add(exportItem);
        flyout.Items.Add(importItem);
        flyout.Items.Add(sep);
        flyout.Items.Add(delItem);
        flyout.ShowAt(btn);
    }

    // ── Item editor dialog ─────────────────────────────────────────────────

    private async Task ShowItemDialog(DashboardItem item, bool isNew)
    {
        // Work on a clone so Cancel discards changes
        var draft = CloneItem(item);

        // Picker host sits alongside the editor in a shared Grid; one is Collapsed at a time.
        // pickerAction is assigned after the dialog is created so it can reference the dialog.
        Action<byte, byte, byte, Action<byte, byte, byte>>? pickerAction = null;

        var editorContent = BuildEditorContent(draft, out var nameBox, out var modeCombo,
            out var solidSection, out var glowSection, out var fadeBetweenSection,
            out var rainbowSection, out var locationGlowSection, out var audioReactiveSection,
            pickerAction: (r, g, b, onApply) => pickerAction?.Invoke(r, g, b, onApply));

        var pickerHost = new Border { Visibility = Visibility.Collapsed };

        var dialogRoot = new Grid();
        dialogRoot.Children.Add(editorContent);
        dialogRoot.Children.Add(pickerHost);

        string editorTitle = isNew ? "New Preset" : "Edit Preset";

        var dialog = new ContentDialog
        {
            XamlRoot          = XamlRoot,
            RequestedTheme    = ActualTheme,
            Title             = editorTitle,
            PrimaryButtonText = "Save",
            CloseButtonText   = "Cancel",
            DefaultButton     = ContentDialogButton.Primary,
            Content           = dialogRoot,
            MinWidth          = 480,
        };

        // Wire the inline picker — runs after dialog is created so it can reference it.
        pickerAction = (r, g, b, onApply) =>
        {
            pickerHost.Child = BuildHsvPickerContent(r, g, b,
                onApply: (nr, ng, nb) =>
                {
                    onApply(nr, ng, nb);
                    pickerHost.Visibility        = Visibility.Collapsed;
                    editorContent.Visibility     = Visibility.Visible;
                    dialog.Title                 = editorTitle;
                    dialog.PrimaryButtonText     = "Save";
                    dialog.CloseButtonText       = "Cancel";
                },
                onCancel: () =>
                {
                    pickerHost.Visibility        = Visibility.Collapsed;
                    editorContent.Visibility     = Visibility.Visible;
                    dialog.Title                 = editorTitle;
                    dialog.PrimaryButtonText     = "Save";
                    dialog.CloseButtonText       = "Cancel";
                });
            pickerHost.Visibility    = Visibility.Visible;
            editorContent.Visibility = Visibility.Collapsed;
            dialog.Title             = "Pick a Color";
            dialog.PrimaryButtonText = "";
            dialog.CloseButtonText   = "";
        };

        // Wire mode ComboBox to show/hide sections
        modeCombo.SelectionChanged += (_, _) =>
        {
            var mode = (BorderMode)(modeCombo.SelectedIndex);
            draft.Mode = mode;
            solidSection.Visibility         = mode == BorderMode.SolidColor    ? Visibility.Visible : Visibility.Collapsed;
            glowSection.Visibility          = mode == BorderMode.Glow           ? Visibility.Visible : Visibility.Collapsed;
            fadeBetweenSection.Visibility   = mode == BorderMode.FadeBetween    ? Visibility.Visible : Visibility.Collapsed;
            rainbowSection.Visibility       = mode == BorderMode.Rainbow         ? Visibility.Visible : Visibility.Collapsed;
            locationGlowSection.Visibility  = mode == BorderMode.LocationGlow    ? Visibility.Visible : Visibility.Collapsed;
            audioReactiveSection.Visibility = mode == BorderMode.AudioReactive   ? Visibility.Visible : Visibility.Collapsed;
        };

        var result = await dialog.ShowAsync();
        if (result == ContentDialogResult.Primary)
        {
            draft.Name = nameBox.Text.Trim();
            if (string.IsNullOrEmpty(draft.Name)) draft.Name = "Unnamed";

            // Copy draft back to the original item and persist
            CopyItemFields(draft, item);
            DashboardItemStore.Instance.Update(item);

            // Repaint the card for this item in case CollectionChanged Replace
            // doesn't fully refresh the visual tree.
            if (PresetsGrid.ContainerFromItem(item) is GridViewItem container)
            {
                if (container.ContentTemplateRoot is Border outerBorder &&
                    outerBorder.Child is Grid grid &&
                    grid.Children[0] is Border preview)
                {
                    PaintPreview(preview, item);
                    outerBorder.BorderBrush = item.Id == DashboardItemStore.Instance.LastAppliedId
                        ? new SolidColorBrush(AccentOrange)
                        : new SolidColorBrush(Colors.Transparent);
                }
            }

            // Refresh filtered list so any description/name changes are reflected
            RefreshFilteredItems();

            // If this item is currently applied, re-apply so animation updates
            if (item.Id == DashboardItemStore.Instance.LastAppliedId)
                ApplyItem(item, showBanner: false);

            // Re-register hotkeys to pick up any assignment changes
            RegisterAllHotkeys();
        }
        else if (isNew)
        {
            // User cancelled new item — remove the placeholder
            DashboardItemStore.Instance.Delete(item.Id);
            UpdateEmptyState();
        }
    }

    private FrameworkElement BuildEditorContent(
        DashboardItem draft,
        out TextBox nameBox,
        out ComboBox modeCombo,
        out StackPanel solidSection,
        out StackPanel glowSection,
        out StackPanel fadeBetweenSection,
        out StackPanel rainbowSection,
        out StackPanel locationGlowSection,
        out StackPanel audioReactiveSection,
        Action<byte, byte, byte, Action<byte, byte, byte>>? pickerAction = null)
    {
        var root = new ScrollViewer
        {
            MaxHeight          = 560,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
        };

        var stack = new StackPanel { Spacing = 16, Padding = new Thickness(0, 4, 12, 4) };
        root.Content = stack;

        // ── Name ──────────────────────────────────────────────────────────
        nameBox = new TextBox
        {
            Header        = "Name",
            Text          = draft.Name,
            PlaceholderText = "Enter a name",
        };
        stack.Children.Add(nameBox);

        // ── Description ────────────────────────────────────────────────────
        var descBox = new TextBox
        {
            Header          = "Description",
            Text            = draft.Description,
            PlaceholderText = "Optional — shown under the preset name on the card",
            MaxLength       = 200,
            AcceptsReturn   = false,
        };
        descBox.TextChanged += (_, _) => draft.Description = descBox.Text;
        stack.Children.Add(descBox);

        // ── Mode ──────────────────────────────────────────────────────────
        modeCombo = new ComboBox
        {
            Header          = "Mode",
            HorizontalAlignment = HorizontalAlignment.Stretch,
            SelectedIndex   = (int)draft.Mode,
        };
        modeCombo.Items.Add("Solid Color");
        modeCombo.Items.Add("Glow");
        modeCombo.Items.Add("Fade Between");
        modeCombo.Items.Add("Rainbow");
        modeCombo.Items.Add("Location Glow");
        modeCombo.Items.Add("Audio Reactive");
        stack.Children.Add(modeCombo);

        // ── Brightness (all modes) ─────────────────────────────────────────
        var brightnessSlider = new Slider
        {
            Header        = "Brightness",
            Minimum       = 0, Maximum = 200, StepFrequency = 5,
            Value         = draft.Brightness,
        };
        brightnessSlider.ValueChanged += (_, e) => draft.Brightness = (int)e.NewValue;
        stack.Children.Add(brightnessSlider);

        // ── Solid Color section ────────────────────────────────────────────
        solidSection = new StackPanel { Spacing = 8 };
        solidSection.Children.Add(MakeSectionHeader("Color"));
        solidSection.Children.Add(MakeColorRow(draft.R, draft.G, draft.B, (r, g, b) =>
        {
            draft.R = r; draft.G = g; draft.B = b;
        }, pickerAction));
        solidSection.Visibility = draft.Mode == BorderMode.SolidColor ? Visibility.Visible : Visibility.Collapsed;
        stack.Children.Add(solidSection);

        // ── Glow section ───────────────────────────────────────────────────
        glowSection = new StackPanel { Spacing = 8 };
        glowSection.Children.Add(MakeSectionHeader("Color"));
        glowSection.Children.Add(MakeColorRow(draft.R, draft.G, draft.B, (r, g, b) =>
        {
            draft.R = r; draft.G = g; draft.B = b;
        }, pickerAction));
        glowSection.Children.Add(MakeSectionHeader("Timing"));
        glowSection.Children.Add(MakeSliderRow("Fade In",    draft.FadeInSec,      v => draft.FadeInSec      = v));
        glowSection.Children.Add(MakeSliderRow("Stay On",    draft.StayOnColorSec, v => draft.StayOnColorSec = v));
        glowSection.Children.Add(MakeSliderRow("Fade Out",   draft.FadeOutSec,     v => draft.FadeOutSec     = v));
        glowSection.Children.Add(MakeSliderRow("Stay Off (0 = no black pause)", draft.StayOnBlackSec, v => draft.StayOnBlackSec = v));
        glowSection.Visibility = draft.Mode == BorderMode.Glow ? Visibility.Visible : Visibility.Collapsed;
        stack.Children.Add(glowSection);

        // ── Fade Between section ───────────────────────────────────────────
        fadeBetweenSection = new StackPanel { Spacing = 8 };
        fadeBetweenSection.Children.Add(MakeSectionHeader("Color A"));
        fadeBetweenSection.Children.Add(MakeColorRow(draft.R, draft.G, draft.B, (r, g, b) =>
        {
            draft.R = r; draft.G = g; draft.B = b;
        }, pickerAction));
        fadeBetweenSection.Children.Add(MakeSectionHeader("Color B"));
        fadeBetweenSection.Children.Add(MakeColorRow(draft.R2, draft.G2, draft.B2, (r, g, b) =>
        {
            draft.R2 = r; draft.G2 = g; draft.B2 = b;
        }, pickerAction));
        fadeBetweenSection.Children.Add(MakeSectionHeader("Timing"));
        fadeBetweenSection.Children.Add(MakeSliderRow("Fade A→B",   draft.FadeInSec,      v => draft.FadeInSec      = v));
        fadeBetweenSection.Children.Add(MakeSliderRow("Stay on A",  draft.StayOnColorSec, v => draft.StayOnColorSec = v));
        fadeBetweenSection.Children.Add(MakeSliderRow("Fade B→A",   draft.FadeOutSec,     v => draft.FadeOutSec     = v));
        fadeBetweenSection.Children.Add(MakeSliderRow("Stay on B",  draft.StayOnBlackSec, v => draft.StayOnBlackSec = v));
        fadeBetweenSection.Visibility = draft.Mode == BorderMode.FadeBetween ? Visibility.Visible : Visibility.Collapsed;
        stack.Children.Add(fadeBetweenSection);

        // ── Rainbow section ────────────────────────────────────────────────
        rainbowSection = BuildRainbowSection(draft, pickerAction);
        rainbowSection.Visibility = draft.Mode == BorderMode.Rainbow ? Visibility.Visible : Visibility.Collapsed;
        stack.Children.Add(rainbowSection);

        // ── Location Glow section ──────────────────────────────────────────
        locationGlowSection = new StackPanel { Spacing = 8 };
        locationGlowSection.Children.Add(MakeSectionHeader("Top of Screen Color"));
        locationGlowSection.Children.Add(MakeColorRow(draft.R, draft.G, draft.B, (r, g, b) =>
        {
            draft.R = r; draft.G = g; draft.B = b;
        }, pickerAction));
        locationGlowSection.Children.Add(MakeSectionHeader("Bottom of Screen Color"));
        locationGlowSection.Children.Add(MakeColorRow(draft.R2, draft.G2, draft.B2, (r, g, b) =>
        {
            draft.R2 = r; draft.G2 = g; draft.B2 = b;
        }, pickerAction));
        locationGlowSection.Visibility = draft.Mode == BorderMode.LocationGlow
            ? Visibility.Visible : Visibility.Collapsed;
        stack.Children.Add(locationGlowSection);

        // ── Audio Reactive section ─────────────────────────────────────────
        audioReactiveSection = new StackPanel { Spacing = 8 };
        audioReactiveSection.Children.Add(MakeSectionHeader("Bass Color"));
        audioReactiveSection.Children.Add(MakeColorRow(draft.R, draft.G, draft.B, (r, g, b) =>
        {
            draft.R = r; draft.G = g; draft.B = b;
        }, pickerAction));
        audioReactiveSection.Children.Add(MakeSectionHeader("Treble Color"));
        audioReactiveSection.Children.Add(MakeColorRow(draft.R2, draft.G2, draft.B2, (r, g, b) =>
        {
            draft.R2 = r; draft.G2 = g; draft.B2 = b;
        }, pickerAction));
        var driftSlider = new Slider
        {
            Header        = "Color Drift",
            Minimum       = 0, Maximum = 100, StepFrequency = 5,
            Value         = draft.AudioHueWeight,
        };
        driftSlider.ValueChanged += (_, e) => draft.AudioHueWeight = (int)e.NewValue;
        audioReactiveSection.Children.Add(driftSlider);
        audioReactiveSection.Children.Add(new TextBlock
        {
            Text         = "Bass shifts toward the first color; treble toward the second. Brightness follows loudness.",
            FontSize     = 11,
            Foreground   = new SolidColorBrush(Color.FromArgb(0xFF, 0x80, 0x80, 0x80)),
            TextWrapping = TextWrapping.Wrap,
            Margin       = new Thickness(0, 4, 0, 0),
        });
        audioReactiveSection.Visibility = draft.Mode == BorderMode.AudioReactive
            ? Visibility.Visible : Visibility.Collapsed;
        stack.Children.Add(audioReactiveSection);

        // ── Hotkey section (all modes) ─────────────────────────────────────
        var hotkeySection = new StackPanel { Spacing = 8, Margin = new Thickness(0, 8, 0, 0) };
        hotkeySection.Children.Add(MakeSectionHeader("Hotkey"));

        var hotkeyDisplay = new TextBox
        {
            IsReadOnly      = true,
            PlaceholderText = "No hotkey — click Set to assign",
            Text            = draft.HotkeyKey != 0
                              ? HotkeyService.FormatHotkey(draft.HotkeyModifiers, draft.HotkeyKey)
                              : "",
        };
        hotkeySection.Children.Add(hotkeyDisplay);

        var setBtn   = new Button { Content = "Set hotkey…" };
        var clearBtn = new Button { Content = "Clear", Margin = new Thickness(8, 0, 0, 0) };

        setBtn.Click += async (_, _) =>
        {
            int capMods = 0, capVk = 0;
            var captureBox = new TextBox
            {
                PlaceholderText = "Press any key combination…",
                IsReadOnly      = true,
                Width           = 260,
            };

            captureBox.KeyDown += (_, ke) =>
            {
                var vk = ke.Key;
                if (vk is Windows.System.VirtualKey.Control or Windows.System.VirtualKey.Shift
                       or Windows.System.VirtualKey.Menu   or Windows.System.VirtualKey.LeftWindows
                       or Windows.System.VirtualKey.RightWindows) return;

                int m = 0;
                if ((GetKeyState(0x11) & 0x8000) != 0) m |= HotkeyService.MOD_CONTROL;
                if ((GetKeyState(0x10) & 0x8000) != 0) m |= HotkeyService.MOD_SHIFT;
                if ((GetKeyState(0x12) & 0x8000) != 0) m |= HotkeyService.MOD_ALT;
                if ((GetKeyState(0x5B) & 0x8000) != 0 ||
                    (GetKeyState(0x5C) & 0x8000) != 0) m |= HotkeyService.MOD_WIN;

                capMods = m;
                capVk   = (int)vk;
                captureBox.Text = HotkeyService.FormatHotkey(m, (int)vk);
                ke.Handled = true;
            };

            var dlg = new ContentDialog
            {
                XamlRoot          = XamlRoot,
                RequestedTheme    = ActualTheme,
                Title             = "Set Hotkey",
                Content           = captureBox,
                PrimaryButtonText = "Save",
                CloseButtonText   = "Cancel",
            };

            if (await dlg.ShowAsync() == ContentDialogResult.Primary && capVk != 0)
            {
                draft.HotkeyModifiers = capMods;
                draft.HotkeyKey       = capVk;
                hotkeyDisplay.Text    = HotkeyService.FormatHotkey(capMods, capVk);
            }
        };

        clearBtn.Click += (_, _) =>
        {
            draft.HotkeyModifiers = 0;
            draft.HotkeyKey       = 0;
            hotkeyDisplay.Text    = "";
        };

        var hotkeyBtnRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Margin      = new Thickness(0, 4, 0, 0),
        };
        hotkeyBtnRow.Children.Add(setBtn);
        hotkeyBtnRow.Children.Add(clearBtn);
        hotkeySection.Children.Add(hotkeyBtnRow);
        stack.Children.Add(hotkeySection);

        return root;
    }

    private StackPanel BuildRainbowSection(DashboardItem draft, Action<byte, byte, byte, Action<byte, byte, byte>>? pickerAction = null)
    {
        var section = new StackPanel { Spacing = 8 };
        section.Children.Add(MakeSectionHeader("Colors (in sequence)"));

        var colorList = new StackPanel { Spacing = 4 };

        void RebuildColorList()
        {
            colorList.Children.Clear();
            for (int i = 0; i < draft.RainbowColors.Count; i++)
            {
                int idx = i;
                var rc  = draft.RainbowColors[idx];
                var row = new Grid();
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

                var colorRow = MakeColorRow(rc.R, rc.G, rc.B, (r, g, b) =>
                {
                    draft.RainbowColors[idx].R = r;
                    draft.RainbowColors[idx].G = g;
                    draft.RainbowColors[idx].B = b;
                }, pickerAction);
                Grid.SetColumn(colorRow, 0);
                row.Children.Add(colorRow);

                var removeBtn = new Button
                {
                    Content = new FontIcon { Glyph = "", FontSize = 12 },
                    Width = 30, Height = 30,
                    Padding = new Thickness(0),
                    VerticalAlignment = VerticalAlignment.Center,
                    Background = new SolidColorBrush(Colors.Transparent),
                    BorderThickness = new Thickness(0),
                    Margin = new Thickness(8, 0, 0, 0),
                };
                removeBtn.Click += (_, _) =>
                {
                    if (draft.RainbowColors.Count > 2)
                    {
                        draft.RainbowColors.RemoveAt(idx);
                        RebuildColorList();
                    }
                };
                Grid.SetColumn(removeBtn, 1);
                row.Children.Add(removeBtn);

                colorList.Children.Add(row);
            }

            var addBtn = new Button
            {
                Content = "+ Add Color",
                HorizontalAlignment = HorizontalAlignment.Left,
                Margin = new Thickness(0, 4, 0, 0),
                Padding = new Thickness(10, 6, 10, 6),
                CornerRadius = new CornerRadius(4),
            };
            addBtn.Click += (_, _) =>
            {
                draft.RainbowColors.Add(new RainbowColor { R = 0xFF, G = 0xFF, B = 0xFF });
                RebuildColorList();
            };
            colorList.Children.Add(addBtn);
        }

        RebuildColorList();
        section.Children.Add(colorList);

        section.Children.Add(MakeSectionHeader("Transition"));
        section.Children.Add(MakeSliderRow("Per-color crossfade duration", draft.TransitionSec, v => draft.TransitionSec = v));

        return section;
    }

    // ── Dialog helpers ─────────────────────────────────────────────────────

    private static (StackPanel row, Slider slider) MakeChannelSlider(string label, byte value)
    {
        var lbl = new TextBlock
        {
            Text              = label,
            Width             = 14,
            FontSize          = 11,
            FontWeight        = Microsoft.UI.Text.FontWeights.SemiBold,
            VerticalAlignment = VerticalAlignment.Center,
        };
        var slider = new Slider
        {
            Minimum           = 0,
            Maximum           = 255,
            Value             = value,
            Width             = 155,
            VerticalAlignment = VerticalAlignment.Center,
        };
        var numText = new TextBlock
        {
            Text              = value.ToString("D3"),
            Width             = 28,
            FontSize          = 11,
            TextAlignment     = TextAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center,
        };
        slider.ValueChanged += (_, e) => numText.Text = ((int)e.NewValue).ToString("D3");

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6 };
        row.Children.Add(lbl);
        row.Children.Add(slider);
        row.Children.Add(numText);
        return (row, slider);
    }

    private static TextBlock MakeSectionHeader(string text) => new TextBlock
    {
        Text       = text,
        FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
        FontSize   = 12,
        Foreground = new SolidColorBrush(Color.FromArgb(0xFF, 0xA0, 0xA0, 0xA0)),
        Margin     = new Thickness(0, 4, 0, 0),
    };

    private static FrameworkElement MakeColorRow(byte r, byte g, byte b, Action<byte, byte, byte> onChange,
        Action<byte, byte, byte, Action<byte, byte, byte>>? pickerAction = null)
    {
        byte cr = r, cg = g, cb = b;
        bool updating = false;

        var swatchBorder = new Border
        {
            Width           = 22,
            Height          = 22,
            CornerRadius    = new CornerRadius(3),
            Background      = new SolidColorBrush(Color.FromArgb(255, r, g, b)),
            BorderThickness = new Thickness(1),
            BorderBrush     = new SolidColorBrush(Color.FromArgb(0x50, 0x80, 0x80, 0x80)),
        };

        var swatchBtn = new Button
        {
            Content           = swatchBorder,
            Padding           = new Thickness(3),
            CornerRadius      = new CornerRadius(4),
            BorderThickness   = new Thickness(1),
            Background        = new SolidColorBrush(Colors.Transparent),
            Margin            = new Thickness(0, 0, 8, 0),
            VerticalAlignment = VerticalAlignment.Center,
        };

        var hexBox = new TextBox
        {
            Text              = $"#{r:X2}{g:X2}{b:X2}",
            PlaceholderText   = "#RRGGBB",
            FontFamily        = new Microsoft.UI.Xaml.Media.FontFamily("Cascadia Code"),
            FontSize          = 13,
            Width             = 110,
            VerticalAlignment = VerticalAlignment.Center,
        };

        void SyncAll(byte nr, byte ng, byte nb)
        {
            if (updating) return;
            updating = true;
            cr = nr; cg = ng; cb = nb;
            swatchBorder.Background = new SolidColorBrush(Color.FromArgb(255, nr, ng, nb));
            hexBox.Text             = $"#{nr:X2}{ng:X2}{nb:X2}";
            onChange(nr, ng, nb);
            updating = false;
        }

        hexBox.LostFocus += (_, _) =>
        {
            if (TryParseHex(hexBox.Text, out byte nr, out byte ng, out byte nb))
                SyncAll(nr, ng, nb);
            else
                hexBox.Text = $"#{cr:X2}{cg:X2}{cb:X2}";
        };

        swatchBtn.Click += (_, _) =>
        {
            if (pickerAction is not null)
                pickerAction(cr, cg, cb, (nr, ng, nb) => SyncAll(nr, ng, nb));
            else
                ShowColorPickerWindow(cr, cg, cb, (nr, ng, nb) => SyncAll(nr, ng, nb));
        };

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 0 };
        row.Children.Add(swatchBtn);
        row.Children.Add(hexBox);
        return row;
    }

    // Fallback: open the HSV picker in a standalone Window (used when not inside a dialog).
    internal static void ShowColorPickerWindow(byte r, byte g, byte b, Action<byte, byte, byte> onApply)
    {
        Window? pickerWin = null;
        var content = BuildHsvPickerContent(r, g, b,
            onApply: (nr, ng, nb) => { onApply(nr, ng, nb); pickerWin?.Close(); },
            onCancel: () => pickerWin?.Close());

        pickerWin = new Window { Title = "Pick a Color", Content = content };
        pickerWin.AppWindow.Resize(new Windows.Graphics.SizeInt32(400, 520));

        content.Loaded += (_, _) =>
        {
            double scale = content.XamlRoot?.RasterizationScale ?? 1.0;
            int sw = (int)Math.Ceiling(340 * scale);
            int sh = (int)Math.Ceiling(441 * scale);
            pickerWin.AppWindow.Resize(new Windows.Graphics.SizeInt32(sw, sh));
            var da = Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(
                pickerWin.AppWindow.Id, Microsoft.UI.Windowing.DisplayAreaFallback.Primary);
            var wa = da.WorkArea;
            pickerWin.AppWindow.Move(new Windows.Graphics.PointInt32(
                wa.X + (wa.Width  - sw) / 2,
                wa.Y + (wa.Height - sh) / 2));
        };

        var da0 = Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(
            pickerWin.AppWindow.Id, Microsoft.UI.Windowing.DisplayAreaFallback.Primary);
        var wa0 = da0.WorkArea;
        pickerWin.AppWindow.Move(new Windows.Graphics.PointInt32(
            wa0.X + (wa0.Width  - 400) / 2,
            wa0.Y + (wa0.Height - 520) / 2));

        if (pickerWin.AppWindow.Presenter is Microsoft.UI.Windowing.OverlappedPresenter op)
        {
            op.IsMaximizable = false;
            op.IsResizable   = false;
            op.IsAlwaysOnTop = true;
        }

        pickerWin.Activate();
    }

    // Builds the HSV picker UI as a StackPanel — used both inline (dialog) and in the fallback Window.
    private static StackPanel BuildHsvPickerContent(
        byte r, byte g, byte b,
        Action<byte, byte, byte> onApply,
        Action onCancel)
    {
        RgbToHsv(r, g, b, out double curH, out double curS, out double curV);
        byte cr = r, cg = g, cb = b;

        const double SW = 300, SH = 200; // spectrum size (DIPs)
        const double HW = 300, HH = 22;  // hue strip size (DIPs)

        // ── Hue gradient strip ─────────────────────────────────────────────
        var hueBrush = new LinearGradientBrush
        {
            StartPoint = new Windows.Foundation.Point(0, 0.5),
            EndPoint   = new Windows.Foundation.Point(1, 0.5),
        };
        foreach (double deg in new[] { 0.0, 60.0, 120.0, 180.0, 240.0, 300.0, 360.0 })
        {
            HsvToRgb(deg >= 360 ? 0 : deg, 1, 1, out byte hr2, out byte hg2, out byte hb2);
            hueBrush.GradientStops.Add(new GradientStop { Color = Color.FromArgb(255, hr2, hg2, hb2), Offset = deg / 360.0 });
        }

        var hueBar = new Border { Width = HW, Height = HH, CornerRadius = new CornerRadius(4), Background = hueBrush };

        // Hue cursor: white bar with semi-transparent shadow for contrast
        var hueShadow = new Border { Width = 6, Height = HH + 10, CornerRadius = new CornerRadius(2), Background = new SolidColorBrush(Color.FromArgb(0x80, 0, 0, 0)), IsHitTestVisible = false };
        var hueThumb  = new Border { Width = 4, Height = HH + 8,  CornerRadius = new CornerRadius(1), Background = new SolidColorBrush(Colors.White), IsHitTestVisible = false };
        var hueCanvas = new Canvas { Width = HW, Height = HH + 10, IsHitTestVisible = false };
        hueCanvas.Children.Add(hueShadow);
        hueCanvas.Children.Add(hueThumb);

        var hueHost = new Grid { Width = HW, Height = HH + 10 };
        hueHost.RowDefinitions.Add(new RowDefinition { Height = new GridLength(4) });
        hueHost.RowDefinitions.Add(new RowDefinition { Height = new GridLength(HH) });
        hueHost.RowDefinitions.Add(new RowDefinition { Height = new GridLength(4) });
        Grid.SetRow(hueBar, 1);
        hueHost.Children.Add(hueBar);
        hueHost.Children.Add(hueCanvas);

        void PlaceHueCursor()
        {
            double x = curH / 360.0 * HW;
            Canvas.SetLeft(hueShadow, x - 3); Canvas.SetTop(hueShadow, 0);
            Canvas.SetLeft(hueThumb,  x - 2); Canvas.SetTop(hueThumb,  1);
        }
        PlaceHueCursor();

        // ── Spectrum field (sat × val) ─────────────────────────────────────
        // Base: white → pure hue (horizontal)
        var specBaseBrush = new LinearGradientBrush
        {
            StartPoint = new Windows.Foundation.Point(0, 0.5),
            EndPoint   = new Windows.Foundation.Point(1, 0.5),
        };
        specBaseBrush.GradientStops.Add(new GradientStop { Color = Colors.White, Offset = 0 });
        specBaseBrush.GradientStops.Add(new GradientStop { Color = Colors.Red,   Offset = 1 }); // updated on hue change

        // Shade: transparent → black (vertical) — value dimension
        var specShadeBrush = new LinearGradientBrush
        {
            StartPoint = new Windows.Foundation.Point(0.5, 0),
            EndPoint   = new Windows.Foundation.Point(0.5, 1),
        };
        specShadeBrush.GradientStops.Add(new GradientStop { Color = Colors.Transparent, Offset = 0 });
        specShadeBrush.GradientStops.Add(new GradientStop { Color = Colors.Black,       Offset = 1 });

        // Using Border instead of Rectangle (no Shapes namespace needed)
        var specBase  = new Border { Width = SW, Height = SH, Background = specBaseBrush };
        var specShade = new Border { Width = SW, Height = SH, Background = specShadeBrush, IsHitTestVisible = false };

        // Spectrum cursor: circular Border (white ring + dark outer ring)
        var specCurOuter = new Border { Width = 16, Height = 16, CornerRadius = new CornerRadius(8), BorderBrush = new SolidColorBrush(Color.FromArgb(0x60, 0, 0, 0)), BorderThickness = new Thickness(1), Background = new SolidColorBrush(Colors.Transparent), IsHitTestVisible = false };
        var specCurInner = new Border { Width = 14, Height = 14, CornerRadius = new CornerRadius(7), BorderBrush = new SolidColorBrush(Colors.White), BorderThickness = new Thickness(2), Background = new SolidColorBrush(Colors.Transparent), IsHitTestVisible = false };
        var specCanvas   = new Canvas { Width = SW, Height = SH, IsHitTestVisible = false };
        specCanvas.Children.Add(specCurOuter);
        specCanvas.Children.Add(specCurInner);

        var specHost = new Grid { Width = SW, Height = SH };
        specHost.Children.Add(specBase);
        specHost.Children.Add(specShade);
        specHost.Children.Add(specCanvas);

        void PlaceSpecCursor()
        {
            double x = curS * SW, y = (1.0 - curV) * SH;
            Canvas.SetLeft(specCurOuter, x - 8); Canvas.SetTop(specCurOuter, y - 8);
            Canvas.SetLeft(specCurInner, x - 7); Canvas.SetTop(specCurInner, y - 7);
        }
        PlaceSpecCursor();

        // ── Preview swatch + hex ───────────────────────────────────────────
        var preview = new Border
        {
            Width = 40, Height = 40, CornerRadius = new CornerRadius(6),
            BorderThickness = new Thickness(1),
            BorderBrush     = new SolidColorBrush(Color.FromArgb(0x50, 0x80, 0x80, 0x80)),
            Background      = new SolidColorBrush(Color.FromArgb(255, r, g, b)),
            VerticalAlignment = VerticalAlignment.Center,
        };
        var hexBox = new TextBox
        {
            Text            = $"#{r:X2}{g:X2}{b:X2}",
            PlaceholderText = "#RRGGBB",
            FontFamily      = new Microsoft.UI.Xaml.Media.FontFamily("Cascadia Code"),
            Width           = 130,
            VerticalAlignment = VerticalAlignment.Center,
        };
        var infoRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Margin = new Thickness(0, 4, 0, 0) };
        infoRow.Children.Add(preview);
        infoRow.Children.Add(hexBox);

        // ── Central sync ───────────────────────────────────────────────────
        void UpdateAll()
        {
            HsvToRgb(curH, curS, curV, out cr, out cg, out cb);
            HsvToRgb(curH, 1, 1, out byte hr, out byte hg, out byte hb);
            specBaseBrush.GradientStops[1].Color = Color.FromArgb(255, hr, hg, hb);
            preview.Background = new SolidColorBrush(Color.FromArgb(255, cr, cg, cb));
            hexBox.Text        = $"#{cr:X2}{cg:X2}{cb:X2}";
            PlaceSpecCursor();
            PlaceHueCursor();
        }

        // Initialize spectrum base color from starting hue
        HsvToRgb(curH, 1, 1, out byte ihr, out byte ihg, out byte ihb);
        specBaseBrush.GradientStops[1].Color = Color.FromArgb(255, ihr, ihg, ihb);

        // ── Pointer — spectrum ─────────────────────────────────────────────
        specHost.PointerPressed += (_, e) =>
        {
            var pt = e.GetCurrentPoint(specHost).Position;
            curS = Math.Clamp(pt.X / SW, 0.0, 1.0);
            curV = Math.Clamp(1.0 - pt.Y / SH, 0.0, 1.0);
            UpdateAll(); specHost.CapturePointer(e.Pointer); e.Handled = true;
        };
        specHost.PointerMoved += (_, e) =>
        {
            if (!e.GetCurrentPoint(specHost).Properties.IsLeftButtonPressed) return;
            var pt = e.GetCurrentPoint(specHost).Position;
            curS = Math.Clamp(pt.X / SW, 0.0, 1.0);
            curV = Math.Clamp(1.0 - pt.Y / SH, 0.0, 1.0);
            UpdateAll(); e.Handled = true;
        };
        specHost.PointerReleased += (_, e) => { specHost.ReleasePointerCapture(e.Pointer); e.Handled = true; };

        // ── Pointer — hue strip ────────────────────────────────────────────
        hueHost.PointerPressed += (_, e) =>
        {
            var pt = e.GetCurrentPoint(hueHost).Position;
            curH = Math.Clamp(pt.X / HW * 360.0, 0.0, 359.99);
            UpdateAll(); hueHost.CapturePointer(e.Pointer); e.Handled = true;
        };
        hueHost.PointerMoved += (_, e) =>
        {
            if (!e.GetCurrentPoint(hueHost).Properties.IsLeftButtonPressed) return;
            var pt = e.GetCurrentPoint(hueHost).Position;
            curH = Math.Clamp(pt.X / HW * 360.0, 0.0, 359.99);
            UpdateAll(); e.Handled = true;
        };
        hueHost.PointerReleased += (_, e) => { hueHost.ReleasePointerCapture(e.Pointer); e.Handled = true; };

        // ── Hex input ──────────────────────────────────────────────────────
        hexBox.LostFocus += (_, _) =>
        {
            if (TryParseHex(hexBox.Text, out byte nr, out byte ng, out byte nb))
            {
                cr = nr; cg = ng; cb = nb;
                RgbToHsv(nr, ng, nb, out curH, out curS, out curV);
                UpdateAll();
            }
            else hexBox.Text = $"#{cr:X2}{cg:X2}{cb:X2}";
        };

        // ── Buttons ────────────────────────────────────────────────────────
        var applyBtn = new Button
        {
            Content             = "Apply",
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Padding             = new Thickness(0, 10, 0, 10),
            CornerRadius        = new CornerRadius(6),
            Style               = Application.Current.Resources.ContainsKey("AccentButtonStyle")
                                    ? (Style)Application.Current.Resources["AccentButtonStyle"] : null,
        };
        var cancelBtn = new Button
        {
            Content             = "Cancel",
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Padding             = new Thickness(0, 10, 0, 10),
            CornerRadius        = new CornerRadius(6),
        };
        var btnRow = new Grid { Margin = new Thickness(0, 8, 0, 0) };
        btnRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        btnRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(8, GridUnitType.Pixel) });
        btnRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Grid.SetColumn(applyBtn, 0); Grid.SetColumn(cancelBtn, 2);
        btnRow.Children.Add(applyBtn); btnRow.Children.Add(cancelBtn);

        // ── Wire buttons ───────────────────────────────────────────────────
        applyBtn.Click  += (_, _) => onApply(cr, cg, cb);
        cancelBtn.Click += (_, _) => onCancel();

        // ── Assemble ───────────────────────────────────────────────────────
        var content = new StackPanel { Spacing = 10, Padding = new Thickness(4, 8, 4, 4), HorizontalAlignment = HorizontalAlignment.Center };
        content.Children.Add(specHost);
        content.Children.Add(hueHost);
        content.Children.Add(infoRow);
        content.Children.Add(btnRow);
        return content;
    }

    // ── HSV ↔ RGB helpers ─────────────────────────────────────────────────────

    private static void RgbToHsv(byte r, byte g, byte b, out double h, out double s, out double v)
    {
        double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
        double max = Math.Max(rf, Math.Max(gf, bf));
        double min = Math.Min(rf, Math.Min(gf, bf));
        double delta = max - min;

        v = max;
        s = max == 0 ? 0 : delta / max;

        if (delta == 0) { h = 0; return; }
        if (max == rf)      h = 60.0 * (((gf - bf) / delta) % 6);
        else if (max == gf) h = 60.0 * ((bf - rf) / delta + 2);
        else                h = 60.0 * ((rf - gf) / delta + 4);
        if (h < 0) h += 360.0;
    }

    private static void HsvToRgb(double h, double s, double v, out byte r, out byte g, out byte b)
    {
        if (s == 0) { var w = (byte)(v * 255); r = g = b = w; return; }
        h = ((h % 360) + 360) % 360;
        double hh = h / 60.0;
        int    i  = (int)hh;
        double f  = hh - i;
        double p  = v * (1 - s);
        double q  = v * (1 - f * s);
        double t  = v * (1 - (1 - f) * s);
        double rf, gf, bf;
        switch (i % 6)
        {
            case 0:  rf = v; gf = t; bf = p; break;
            case 1:  rf = q; gf = v; bf = p; break;
            case 2:  rf = p; gf = v; bf = t; break;
            case 3:  rf = p; gf = q; bf = v; break;
            case 4:  rf = t; gf = p; bf = v; break;
            default: rf = v; gf = p; bf = q; break;
        }
        r = (byte)Math.Clamp((int)(rf * 255 + 0.5), 0, 255);
        g = (byte)Math.Clamp((int)(gf * 255 + 0.5), 0, 255);
        b = (byte)Math.Clamp((int)(bf * 255 + 0.5), 0, 255);
    }

    // Slider-based picker panel (fallback when ColorPicker is unavailable)
    private static StackPanel BuildSliderPickerPanel(
        byte r, byte g, byte b, UIElement buttons, Action<byte, byte, byte> onChange)
    {
        byte cr = r, cg = g, cb = b;
        bool updating = false;

        var preview = new Border
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Height              = 64,
            CornerRadius        = new CornerRadius(8),
            Background          = new SolidColorBrush(Color.FromArgb(255, r, g, b)),
        };

        var hexBox = new TextBox
        {
            Text                = $"#{r:X2}{g:X2}{b:X2}",
            PlaceholderText     = "#RRGGBB",
            FontFamily          = new Microsoft.UI.Xaml.Media.FontFamily("Cascadia Code"),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };

        // Wider sliders than the inline editor uses
        var (rRow, rSlider) = MakePickerSlider("R", r);
        var (gRow, gSlider) = MakePickerSlider("G", g);
        var (bRow, bSlider) = MakePickerSlider("B", b);

        void SyncAll(byte nr, byte ng, byte nb)
        {
            if (updating) return;
            updating = true;
            cr = nr; cg = ng; cb = nb;
            preview.Background = new SolidColorBrush(Color.FromArgb(255, nr, ng, nb));
            hexBox.Text        = $"#{nr:X2}{ng:X2}{nb:X2}";
            rSlider.Value      = nr;
            gSlider.Value      = ng;
            bSlider.Value      = nb;
            onChange(nr, ng, nb);
            updating = false;
        }

        rSlider.ValueChanged += (_, e) => SyncAll((byte)e.NewValue, cg, cb);
        gSlider.ValueChanged += (_, e) => SyncAll(cr, (byte)e.NewValue, cb);
        bSlider.ValueChanged += (_, e) => SyncAll(cr, cg, (byte)e.NewValue);

        hexBox.LostFocus += (_, _) =>
        {
            if (TryParseHex(hexBox.Text, out byte nr, out byte ng, out byte nb))
                SyncAll(nr, ng, nb);
            else
                hexBox.Text = $"#{cr:X2}{cg:X2}{cb:X2}";
        };

        var panel = new StackPanel { Spacing = 10, Padding = new Thickness(20) };
        panel.Children.Add(preview);
        panel.Children.Add(hexBox);
        panel.Children.Add(rRow);
        panel.Children.Add(gRow);
        panel.Children.Add(bRow);
        panel.Children.Add(buttons);
        return panel;
    }

    // Wider slider row for the picker window (slider stretches to fill available width)
    private static (Grid row, Slider slider) MakePickerSlider(string label, byte value)
    {
        var lbl = new TextBlock
        {
            Text              = label,
            Width             = 16,
            FontSize          = 12,
            FontWeight        = Microsoft.UI.Text.FontWeights.SemiBold,
            VerticalAlignment = VerticalAlignment.Center,
            Margin            = new Thickness(0, 0, 8, 0),
        };
        var slider = new Slider
        {
            Minimum             = 0,
            Maximum             = 255,
            Value               = value,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment   = VerticalAlignment.Center,
        };
        var numText = new TextBlock
        {
            Text              = value.ToString("D3"),
            Width             = 30,
            FontSize          = 12,
            TextAlignment     = TextAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center,
            Margin            = new Thickness(8, 0, 0, 0),
        };
        slider.ValueChanged += (_, e) => numText.Text = ((int)e.NewValue).ToString("D3");

        var row = new Grid();
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        Grid.SetColumn(lbl,     0);
        Grid.SetColumn(slider,  1);
        Grid.SetColumn(numText, 2);
        row.Children.Add(lbl);
        row.Children.Add(slider);
        row.Children.Add(numText);
        return (row, slider);
    }

    private static Grid MakeSliderRow(string label, double initialValue, Action<double> onChange)
    {
        var row = new Grid { Margin = new Thickness(0, 2, 0, 2) };
        row.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        row.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var headerRow = new Grid();
        headerRow.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        headerRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var labelBlock = new TextBlock
        {
            Text              = label,
            FontSize          = 12,
            VerticalAlignment = VerticalAlignment.Center,
        };
        Grid.SetColumn(labelBlock, 0);

        var valueBox = new TextBox
        {
            Text    = $"{initialValue:F1}",
            Width   = 60,
            FontSize = 12,
            HorizontalTextAlignment = TextAlignment.Right,
        };
        Grid.SetColumn(valueBox, 1);

        headerRow.Children.Add(labelBlock);
        headerRow.Children.Add(valueBox);
        Grid.SetRow(headerRow, 0);

        var slider = new Slider
        {
            Minimum        = 0,
            Maximum        = 30,
            StepFrequency  = 0.1,
            Value          = Math.Clamp(initialValue, 0, 30),
            HorizontalAlignment = HorizontalAlignment.Stretch,
        };
        Grid.SetRow(slider, 1);

        bool updating = false;

        slider.ValueChanged += (_, args) =>
        {
            if (updating) return;
            updating = true;
            double v = Math.Round(args.NewValue, 1);
            valueBox.Text = $"{v:F1}";
            onChange(v);
            updating = false;
        };

        valueBox.LostFocus += (_, _) =>
        {
            if (updating) return;
            if (double.TryParse(valueBox.Text, out double v))
            {
                v = Math.Clamp(Math.Round(v, 1), 0, 30);
                updating = true;
                slider.Value  = v;
                valueBox.Text = $"{v:F1}";
                onChange(v);
                updating = false;
            }
            else
            {
                valueBox.Text = $"{slider.Value:F1}";
            }
        };

        row.Children.Add(headerRow);
        row.Children.Add(slider);
        return row;
    }

    private static bool TryParseHex(string text, out byte r, out byte g, out byte b)
    {
        r = g = b = 0;
        text = text.TrimStart('#');
        if (text.Length != 6) return false;
        try
        {
            r = Convert.ToByte(text[0..2], 16);
            g = Convert.ToByte(text[2..4], 16);
            b = Convert.ToByte(text[4..6], 16);
            return true;
        }
        catch { return false; }
    }

    // ── Rename dialog ──────────────────────────────────────────────────────

    private async Task ShowRenameDialog(DashboardItem item)
    {
        var box = new TextBox { Text = item.Name, SelectionStart = 0, SelectionLength = item.Name.Length };
        var dlg = new ContentDialog
        {
            XamlRoot          = XamlRoot,
            RequestedTheme    = ActualTheme,
            Title             = "Rename",
            Content           = box,
            PrimaryButtonText = "OK",
            CloseButtonText   = "Cancel",
            DefaultButton     = ContentDialogButton.Primary,
        };
        if (await dlg.ShowAsync() == ContentDialogResult.Primary)
        {
            var name = box.Text.Trim();
            if (!string.IsNullOrEmpty(name))
            {
                item.Name = name;
                DashboardItemStore.Instance.Update(item);
            }
        }
    }

    // ── Delete ─────────────────────────────────────────────────────────────

    private async Task DeleteItemAsync(DashboardItem item)
    {
        var dlg = new ContentDialog
        {
            XamlRoot          = XamlRoot,
            RequestedTheme    = ActualTheme,
            Title             = "Delete preset",
            Content           = $"Delete \"{item.Name}\"? This cannot be undone.",
            PrimaryButtonText = "Delete",
            CloseButtonText   = "Cancel",
            DefaultButton     = ContentDialogButton.Close,
        };
        if (await dlg.ShowAsync() == ContentDialogResult.Primary)
        {
            bool wasActive = item.Id == DashboardItemStore.Instance.LastAppliedId;
            DashboardItemStore.Instance.Delete(item.Id);
            UpdateEmptyState();

            if (wasActive)
            {
                var anim = ((App)Application.Current).MainWindow?.GlowAnimator;
                anim?.StopAnimationMode();
            }
        }
    }

    // ── Clone helpers ──────────────────────────────────────────────────────

    private static DashboardItem CloneItem(DashboardItem src) => new()
    {
        Id             = src.Id,
        Name           = src.Name,
        Description    = src.Description,
        Mode           = src.Mode,
        R = src.R, G = src.G, B = src.B,
        R2 = src.R2, G2 = src.G2, B2 = src.B2,
        RainbowColors  = new(src.RainbowColors.Select(c => new RainbowColor { R = c.R, G = c.G, B = c.B })),
        FadeInSec      = src.FadeInSec,
        StayOnColorSec = src.StayOnColorSec,
        FadeOutSec     = src.FadeOutSec,
        StayOnBlackSec = src.StayOnBlackSec,
        TransitionSec  = src.TransitionSec,
        AudioHueWeight  = src.AudioHueWeight,
        Brightness      = src.Brightness,
        HotkeyModifiers = src.HotkeyModifiers,
        HotkeyKey       = src.HotkeyKey,
    };

    private static void CopyItemFields(DashboardItem src, DashboardItem dst)
    {
        dst.Name            = src.Name;
        dst.Description     = src.Description;
        dst.Mode            = src.Mode;
        dst.R = src.R; dst.G = src.G; dst.B = src.B;
        dst.R2 = src.R2; dst.G2 = src.G2; dst.B2 = src.B2;
        dst.RainbowColors   = new(src.RainbowColors.Select(c => new RainbowColor { R = c.R, G = c.G, B = c.B }));
        dst.FadeInSec       = src.FadeInSec;
        dst.StayOnColorSec  = src.StayOnColorSec;
        dst.FadeOutSec      = src.FadeOutSec;
        dst.StayOnBlackSec  = src.StayOnBlackSec;
        dst.TransitionSec   = src.TransitionSec;
        dst.AudioHueWeight  = src.AudioHueWeight;
        dst.Brightness      = src.Brightness;
        dst.HotkeyModifiers = src.HotkeyModifiers;
        dst.HotkeyKey       = src.HotkeyKey;
    }

    // ── Desktop shortcut ───────────────────────────────────────────────────

    private async void CreateShortcut_Click(object sender, RoutedEventArgs e)
    {
        string exePath = Environment.ProcessPath
                      ?? System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName
                      ?? string.Empty;

        if (string.IsNullOrEmpty(exePath)) { await ShowShortcutResult(success: false); return; }

        string desktop = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);
        string lnkPath = Path.Combine(desktop, "AuraShell.lnk");

        bool ok = ShellShortcut.Create(lnkPath, exePath,
            workingDir: Path.GetDirectoryName(exePath) ?? string.Empty,
            description: "AuraShell — Windows 11 taskbar visual customisation");

        await ShowShortcutResult(ok);
    }

    private async Task ShowShortcutResult(bool success)
    {
        var dlg = new ContentDialog
        {
            XamlRoot        = XamlRoot,
            RequestedTheme  = ActualTheme,
            Title           = success ? "Shortcut created" : "Could not create shortcut",
            Content         = success
                ? "AuraShell.lnk has been placed on your desktop."
                : "An error occurred. Please try running the app as administrator.",
            CloseButtonText = "OK",
        };
        await dlg.ShowAsync();
    }
}
