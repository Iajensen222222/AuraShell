using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace AuraConfig.Pages;

public sealed partial class AboutPage : Page
{
    public AboutPage()
    {
        InitializeComponent();
        Loaded   += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ServiceManager.Instance.StateChanged += OnStateChanged;
        RefreshAll(ServiceManager.Instance.CurrentState);
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        ServiceManager.Instance.StateChanged -= OnStateChanged;
    }

    private void OnStateChanged(object? sender, ServiceState state)
    {
        DispatcherQueue.TryEnqueue(() => RefreshAll(state));
    }

    private void RefreshAll(ServiceState state)
    {
        // Service section
        SvcVersionText.Text = state.ServiceVersion > 0
            ? $"v{state.ServiceVersion >> 8}.{state.ServiceVersion & 0xFF}"
            : "—";
        SvcStatusText.Text = state.IsConnected ? "Running" : "Stopped / not connected";
        SvcUptimeText.Text = state.IsConnected ? state.UptimeFormatted : "—";

        // System section
        OsVersionText.Text = Environment.OSVersion.ToString();
        RuntimeText.Text   = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription;

        // DPI: try to read from the window's AppWindow
        try
        {
            var window = (Application.Current as App)?.MainWindow;
            if (window is not null)
            {
                var appWindow = AppWindow.GetFromWindowId(
                    Microsoft.UI.Win32Interop.GetWindowIdFromWindow(
                        WinRT.Interop.WindowNative.GetWindowHandle(window)));
                DpiText.Text = $"{appWindow.ClientSize.Width} × {appWindow.ClientSize.Height} px  (screen)";
            }
        }
        catch
        {
            DpiText.Text = "—";
        }
    }
}
