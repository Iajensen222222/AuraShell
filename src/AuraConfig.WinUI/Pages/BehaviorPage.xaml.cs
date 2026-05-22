using System;
using System.Diagnostics;
using AuraConfig.Services;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.Win32;
using Windows.UI;

namespace AuraConfig.Pages;

public sealed partial class BehaviorPage : Page
{
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunValueName = "AuraShell";

    public BehaviorPage()
    {
        InitializeComponent();
        Loaded   += OnLoaded;
        Unloaded += OnUnloaded;
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Logger.Info("BehaviorPage", "Loaded");
        ServiceManager.Instance.StateChanged += OnServiceStateChanged;
        UpdateStatusIndicator(ServiceManager.Instance.CurrentState.IsConnected);

        // Reflect current auto-start state from registry
        AutoStartToggle.Toggled -= AutoStartToggle_Toggled;
        AutoStartToggle.IsOn = IsAutoStartEnabled();
        AutoStartToggle.Toggled += AutoStartToggle_Toggled;

        // Suppress initial Toggled events while syncing defaults (both default to on).
        AutoHideToggle.Toggled     -= AutoHideToggle_Toggled;
        MultiMonitorToggle.Toggled -= MultiMonitorToggle_Toggled;
        AutoHideToggle.IsOn     = true;
        MultiMonitorToggle.IsOn = true;
        AutoHideToggle.Toggled     += AutoHideToggle_Toggled;
        MultiMonitorToggle.Toggled += MultiMonitorToggle_Toggled;
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        ServiceManager.Instance.StateChanged -= OnServiceStateChanged;
    }

    private void OnServiceStateChanged(object? sender, Models.ServiceState state)
    {
        DispatcherQueue.TryEnqueue(() => UpdateStatusIndicator(state.IsConnected));
    }

    private void UpdateStatusIndicator(bool connected)
    {
        SvcStatusDot.Fill = new SolidColorBrush(connected ? Colors.LightGreen : Colors.OrangeRed);
        SvcStatusText.Text = connected ? "Service running" : "Service stopped / not connected";
    }

    // ── Service control ────────────────────────────────────────────────────

    private void StartService_Click(object sender, RoutedEventArgs e)
    {
        RunSc("start");
        ShowInfo("Informational", "Start command sent to AuraShellService…");
    }

    private void StopService_Click(object sender, RoutedEventArgs e)
    {
        RunSc("stop");
        ShowInfo("Warning", "Stop command sent to AuraShellService…");
    }

    private void RestartService_Click(object sender, RoutedEventArgs e)
    {
        RunSc("stop");
        RunSc("start");
        ShowInfo("Informational", "Restart command sent to AuraShellService…");
    }

    private void LaunchServiceDirect_Click(object sender, RoutedEventArgs e)
    {
        // Search order: beside the app exe, then known debug/release build locations
        string[] candidates =
        [
            System.IO.Path.Combine(
                System.IO.Path.GetDirectoryName(Environment.ProcessPath ?? "") ?? "",
                "AuraShellService.exe"),
            @"c:\Users\iajen\App Ideas\Desktop Icon Changer\AuraShell\out\build\x64-Debug\bin\AuraShellService.exe",
            @"c:\Users\iajen\App Ideas\Desktop Icon Changer\AuraShell\out\build\x64-Release\bin\Release\AuraShellService.exe",
        ];

        foreach (var path in candidates)
        {
            if (!System.IO.File.Exists(path)) continue;
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName        = path,
                    UseShellExecute = true,
                    Verb            = "runas",
                });
                ShowInfo("Informational", $"Launched: {System.IO.Path.GetFileName(path)}. The service connection should appear within a few seconds.");
                return;
            }
            catch (Exception ex)
            {
                ShowInfo("Warning", $"Could not launch service: {ex.Message}");
                return;
            }
        }

        ShowInfo("Warning",
            "AuraShellService.exe not found. Build the C++ project first (CMake → x64-Debug).");
    }

    private static void RunSc(string verb)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName        = "sc.exe",
                Arguments       = $"{verb} AuraShellService",
                UseShellExecute = true,
                Verb            = "runas",
                CreateNoWindow  = true,
                WindowStyle     = ProcessWindowStyle.Hidden
            });
        }
        catch { }
    }

    private void ShowInfo(string level, string message)
    {
        ServiceInfoText.Text = message;
        ServiceInfoBar.Background = level == "Warning"
            ? new SolidColorBrush(Color.FromArgb(0x30, 0xFC, 0xE1, 0x00))
            : new SolidColorBrush(Color.FromArgb(0x20, 0x60, 0xCD, 0xFF));
        ServiceInfoBar.Visibility = Visibility.Visible;
    }

    // ── Startup toggles ────────────────────────────────────────────────────

    private void AutoStartToggle_Toggled(object sender, RoutedEventArgs e)
    {
        bool enable = AutoStartToggle.IsOn;
        bool ok = enable ? SetAutoStart() : RemoveAutoStart();

        ShowInfo(ok ? "Informational" : "Warning",
            ok
                ? (enable ? "Auto-start enabled — AuraShell will launch at login."
                          : "Auto-start disabled.")
                : "Could not update registry. Try running as administrator.");
    }

    private void ElevatedToggle_Toggled(object sender, RoutedEventArgs e)
    {
        // Elevated auto-start requires a Task Scheduler entry — deferred to v1.1.
        ShowInfo("Informational",
                 ElevatedToggle.IsOn
                     ? "Elevated start requested (requires Task Scheduler setup — coming in v1.1)"
                     : "Standard user start selected.");
    }

    // ── Shell monitoring toggles ───────────────────────────────────────────

    private void AutoHideToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (AutoHideToggle is null || MultiMonitorToggle is null) return; // fires during XAML parse
        Logger.Debug("BehaviorPage", $"AutoHide → {AutoHideToggle.IsOn}");
        SendFeatures();
    }

    private void MultiMonitorToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (AutoHideToggle is null || MultiMonitorToggle is null) return;
        Logger.Debug("BehaviorPage", $"MultiMonitor → {MultiMonitorToggle.IsOn}");
        SendFeatures();
    }

    private void SendFeatures()
    {
        if (AutoHideToggle is null || MultiMonitorToggle is null) return;
        _ = ServiceManager.Instance.SendFeaturesAsync(
                AutoHideToggle.IsOn,
                MultiMonitorToggle.IsOn);
    }

    // ── Registry helpers ───────────────────────────────────────────────────

    private static bool IsAutoStartEnabled()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath);
            return key?.GetValue(RunValueName) is not null;
        }
        catch { return false; }
    }

    private static bool SetAutoStart()
    {
        try
        {
            string exePath = Environment.ProcessPath
                          ?? Process.GetCurrentProcess().MainModule?.FileName
                          ?? string.Empty;
            if (string.IsNullOrEmpty(exePath)) return false;

            using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, writable: true);
            if (key is null) return false;

            key.SetValue(RunValueName, $"\"{exePath}\"");
            return true;
        }
        catch { return false; }
    }

    private static bool RemoveAutoStart()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, writable: true);
            if (key?.GetValue(RunValueName) is null) return true; // already absent
            key.DeleteValue(RunValueName);
            return true;
        }
        catch { return false; }
    }
}
