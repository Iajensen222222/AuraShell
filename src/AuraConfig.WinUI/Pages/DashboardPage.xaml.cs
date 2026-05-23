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

namespace AuraConfig.Pages;

public sealed partial class DashboardPage : Page
{
    public ObservableCollection<GlowPreset> Presets { get; } = new(GlowPresets.All);

    private GlowPreset? _lastApplied;

    public DashboardPage()
    {
        InitializeComponent();
        PresetsGrid.ItemsSource = Presets;
        PresetsGrid.ContainerContentChanging += OnContainerContentChanging;
        Loaded   += OnLoaded;
        Unloaded += OnUnloaded;
    }

    // ── Container painting ─────────────────────────────────────────────────

    private void OnContainerContentChanging(ListViewBase sender,
                                             ContainerContentChangingEventArgs args)
    {
        if (args.Item is not GlowPreset preset) return;
        if (args.ItemContainer.ContentTemplateRoot is not Border outer) return;
        if (outer.Child is not Grid grid) return;
        if (grid.Children.Count == 0) return;
        if (grid.Children[0] is not Border glowBg) return;

        var accentColor = preset.Color;
        var transparent = Color.FromArgb(0, accentColor.R, accentColor.G, accentColor.B);

        glowBg.Background = new LinearGradientBrush
        {
            StartPoint = new Windows.Foundation.Point(0.5, 0),
            EndPoint   = new Windows.Foundation.Point(0.5, 1),
            GradientStops =
            {
                new GradientStop { Color = transparent,                        Offset = 0.0 },
                new GradientStop { Color = accentColor with { A = 50 },        Offset = 0.5 },
                new GradientStop { Color = transparent,                        Offset = 1.0 },
            }
        };

        // Colour the Apply chip to match
        if (grid.Children.Count > 1 &&
            grid.Children[1] is Grid bottomGrid &&
            bottomGrid.Children.Count > 1 &&
            bottomGrid.Children[1] is Border chip)
        {
            chip.Background = new SolidColorBrush(accentColor) { Opacity = 0.18 };
            if (chip.Child is TextBlock chipText)
                chipText.Foreground = new SolidColorBrush(accentColor);
        }
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Logger.Info("DashboardPage", "Loaded");
        ServiceManager.Instance.StateChanged      += OnServiceStateChanged;
        ServiceManager.Instance.PerfStatsReceived += OnPerfStatsReceived;
        UpdateStatusBar(ServiceManager.Instance.CurrentState);

        // Restore the selection to the last-applied preset if there is one,
        // so navigating away and back doesn't snap us back to the first card.
        var lastName = ServiceManager.Instance.LastAppliedTheme?.Name;
        int idx = 0;
        if (!string.IsNullOrEmpty(lastName))
        {
            for (int i = 0; i < Presets.Count; i++)
            {
                if (Presets[i].Name == lastName) { idx = i; break; }
            }
        }
        if (Presets.Count > 0) PresetsGrid.SelectedIndex = idx;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        ServiceManager.Instance.StateChanged      -= OnServiceStateChanged;
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

    private void OnServiceStateChanged(object? sender, ServiceState state)
    {
        // StateChanged may fire from background thread; marshal to UI thread
        DispatcherQueue.TryEnqueue(() => UpdateStatusBar(state));
    }

    private void UpdateStatusBar(ServiceState state)
    {
        if (state.IsConnected)
        {
            StatusDot.Fill     = new SolidColorBrush(Colors.LightGreen);
            StatusText.Text    = $"● Connected  ·  uptime {state.UptimeFormatted}";
            ThemeNameText.Text = state.CurrentTheme;
            ReconnectBtn.Visibility = Visibility.Collapsed;
        }
        else
        {
            StatusDot.Fill     = new SolidColorBrush(Colors.OrangeRed);
            StatusText.Text    = "○ Service not connected";
            ThemeNameText.Text = string.Empty;
            ReconnectBtn.Visibility = Visibility.Visible;
        }
    }

    private async void ReconnectBtn_Click(object sender, RoutedEventArgs e)
    {
        Logger.Info("DashboardPage", "Reconnect clicked");
        ReconnectBtn.IsEnabled = false;
        ReconnectBtn.Content   = "Connecting…";
        StatusText.Text        = "◌ Attempting to connect…";
        StatusDot.Fill         = new SolidColorBrush(Color.FromArgb(0xFF, 0xFC, 0xE1, 0x00)); // amber

        try
        {
            await ServiceManager.Instance.TriggerReconnectAsync();
        }
        catch (Exception ex)
        {
            Logger.Error("DashboardPage", "Reconnect failed", ex);
        }

        // UpdateStatusBar fires through StateChanged, but in case nothing changed
        // (still disconnected), surface a hint instead of a silent revert.
        if (!ServiceManager.Instance.CurrentState.IsConnected)
        {
            Logger.Warn("DashboardPage", "Reconnect attempt finished — still disconnected");
            StatusText.Text = "○ Service unreachable — is AuraShellService running?";
        }

        ReconnectBtn.IsEnabled = true;
        ReconnectBtn.Content   = "Reconnect";
    }

    // ── Preset clicks ──────────────────────────────────────────────────────

    private async void PresetsGrid_ItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is not GlowPreset preset) return;
        _lastApplied = preset;

        bool ok = await ServiceManager.Instance.ApplyPresetAsync(preset);

        AppliedText.Text = ok
            ? $"✓  {preset.Name} applied to live overlay"
            : $"✓  {preset.Name} selected  (will apply when service connects)";

        AppliedBanner.Visibility = Visibility.Visible;

        await Task.Delay(3000);
        if (_lastApplied == preset)
            AppliedBanner.Visibility = Visibility.Collapsed;
    }

    // ── Desktop shortcut ───────────────────────────────────────────────────

    private async void CreateShortcut_Click(object sender, RoutedEventArgs e)
    {
        string exePath = Environment.ProcessPath
                      ?? System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName
                      ?? string.Empty;

        if (string.IsNullOrEmpty(exePath))
        {
            await ShowShortcutResult(success: false);
            return;
        }

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
            XamlRoot          = XamlRoot,
            Title             = success ? "Shortcut created" : "Could not create shortcut",
            Content           = success
                ? "AuraShell.lnk has been placed on your desktop."
                : "An error occurred. Please try running the app as administrator.",
            CloseButtonText   = "OK",
        };
        await dlg.ShowAsync();
    }
}
