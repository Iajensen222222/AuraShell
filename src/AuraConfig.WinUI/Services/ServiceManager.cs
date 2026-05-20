using AuraConfig.Models;
using Microsoft.UI.Dispatching;

namespace AuraConfig.Services;

/// <summary>
/// Application-wide singleton that owns the single AuraShellClient instance and keeps
/// it alive / reconnected via a DispatcherQueue timer.
/// </summary>
public sealed class ServiceManager
{
    // ── Singleton ──────────────────────────────────────────────────────────
    public static ServiceManager Instance { get; } = new();

    // ── State ──────────────────────────────────────────────────────────────
    public ServiceState CurrentState { get; private set; } = ServiceState.Disconnected;

    public event EventHandler<ServiceState>? StateChanged;

    // ── Internals ──────────────────────────────────────────────────────────
    private readonly AuraShellClient _client = new();
    private DispatcherQueueTimer? _pollTimer;
    private bool _polling;

    private ServiceManager() { }

    // ── Public API ─────────────────────────────────────────────────────────

    /// <summary>
    /// Starts the polling loop using the provided DispatcherQueue (call once from
    /// MainWindow after InitializeComponent).
    /// </summary>
    public void StartPolling(DispatcherQueue dispatcherQueue)
    {
        if (_polling) return;
        _polling = true;

        _pollTimer = dispatcherQueue.CreateTimer();
        _pollTimer.Interval = TimeSpan.FromSeconds(5);
        _pollTimer.IsRepeating = true;
        _pollTimer.Tick += async (_, _) => await PollAsync();
        _pollTimer.Start();

        // Do an immediate first poll without waiting 5 seconds.
        _ = PollAsync();
    }

    public void StopPolling()
    {
        _pollTimer?.Stop();
        _polling = false;
    }

    /// <summary>Apply a preset by pushing a ThemeConfig to the service.</summary>
    public async Task<bool> ApplyPresetAsync(GlowPreset preset)
    {
        var theme = new ThemeConfig(preset.Name, preset.Color, preset.AnimSpeedPct);
        return await PushThemeAsync(theme);
    }

    /// <summary>Push an arbitrary ThemeConfig to the service.</summary>
    public async Task<bool> PushThemeAsync(ThemeConfig theme)
    {
        if (!_client.IsConnected)
        {
            var result = await _client.ConnectAsync(timeoutMs: 1500);
            if (result != AuraShellClient.ConnectResult.Connected)
                return false;
        }
        return await _client.PushThemeAsync(theme);
    }

    // ── Polling internals ──────────────────────────────────────────────────

    private async Task PollAsync()
    {
        ServiceState newState;

        try
        {
            if (!_client.IsConnected)
            {
                var result = await _client.ConnectAsync(timeoutMs: 1500);
                if (result != AuraShellClient.ConnectResult.Connected)
                {
                    newState = ServiceState.Disconnected;
                    UpdateState(newState);
                    return;
                }
            }

            var state = await _client.QueryStateAsync();
            newState = state ?? ServiceState.Disconnected;
        }
        catch
        {
            _client.Disconnect();
            newState = ServiceState.Disconnected;
        }

        UpdateState(newState);
    }

    private void UpdateState(ServiceState state)
    {
        CurrentState = state;
        StateChanged?.Invoke(this, state);
    }
}
