using System;
using System.Diagnostics;
using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace AuraConfig.Pages;

public sealed partial class BehaviorPage : Page
{
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
        SvcStatusDot.Fill = (Brush)Application.Current.Resources[
            connected ? "SystemFillColorSuccessBrush" : "SystemFillColorCriticalBrush"];
        SvcStatusText.Text = connected ? "Service is running" : "Service isn't running";
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
            @"c:\Users\iajen\App Ideas\Desktop Icon Changer\AuraShell\out\build\x64-RelWithTests\bin\Release\AuraShellService.exe",
        ];

        Logger.Info("BehaviorPage", "LaunchServiceDirect: searching candidates");

        foreach (var path in candidates)
        {
            if (!System.IO.File.Exists(path))
            {
                Logger.Debug("BehaviorPage", $"  miss: {path}");
                continue;
            }
            try
            {
                // Pass --console so the service runs in-process (foreground), opens the
                // named pipe immediately, and stays alive. Default (no args) tries SCM
                // dispatch and exits with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT.
                // No 'runas' verb — --console mode needs no admin rights to listen on
                // the per-user pipe.
                var psi = new ProcessStartInfo
                {
                    FileName        = path,
                    Arguments       = "--console",
                    UseShellExecute = false,
                    CreateNoWindow  = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError  = true,
                };
                var proc = Process.Start(psi);
                Logger.Info("BehaviorPage",
                    $"Launched {System.IO.Path.GetFileName(path)} --console (PID={proc?.Id})");

                // Kick the polling loop so the dashboard status bar flips green
                // as soon as the service finishes binding the pipe.
                _ = System.Threading.Tasks.Task.Run(async () =>
                {
                    await System.Threading.Tasks.Task.Delay(500);
                    await ServiceManager.Instance.TriggerReconnectAsync();
                });

                ShowInfo("Informational",
                    $"Launched: {System.IO.Path.GetFileName(path)} (--console). " +
                    "The service connection should appear within a few seconds.");
                return;
            }
            catch (Exception ex)
            {
                Logger.Error("BehaviorPage", $"Process.Start failed for {path}", ex);
                ShowInfo("Warning", $"Could not launch service: {ex.Message}");
                return;
            }
        }

        Logger.Warn("BehaviorPage", "No AuraShellService.exe found in any candidate path");
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
        // Theme brushes from App.xaml so the banner reads in light and dark.
        ServiceInfoBar.Background = (Brush)Application.Current.Resources[
            level == "Warning" ? "SystemFillColorCautionBackgroundBrush" : "AppAccentSubtleBrush"];
        ServiceInfoBar.Visibility = Visibility.Visible;
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
}
