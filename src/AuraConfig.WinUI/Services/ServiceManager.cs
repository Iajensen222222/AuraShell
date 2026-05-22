using AuraConfig.Models;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

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

    // ── Audio streaming ────────────────────────────────────────────────────

    /// <summary>Fired on the calling thread when a new AUDIO_BANDS frame arrives.</summary>
    public event EventHandler<AuraConfig.Models.AudioBandsPayload>? AudioBandsReceived;

    /// <summary>Fired when a PERF_STATS frame arrives (~0.5fps from the service).</summary>
    public event EventHandler<AuraConfig.Models.PerfStatsPayload>? PerfStatsReceived;

    private CancellationTokenSource? _audioCts;

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

    /// <summary>
    /// Starts a background loop that reads unsolicited AUDIO_BANDS messages from the
    /// service and fires <see cref="AudioBandsReceived"/>. Safe to call multiple times —
    /// stops the previous reader first.
    /// </summary>
    public void StartAudioStream()
    {
        StopAudioStream();
        _audioCts = new CancellationTokenSource();
        var ct = _audioCts.Token;
        _ = Task.Run(async () =>
        {
            while (!ct.IsCancellationRequested)
            {
                // Ensure connected before trying to read.
                if (!_client.IsConnected)
                {
                    await Task.Delay(500, ct).ConfigureAwait(false);
                    continue;
                }

                var msg = await _client.ReadNextMessageAsync(ct).ConfigureAwait(false);
                if (msg is null) { await Task.Delay(100, ct).ConfigureAwait(false); continue; }

                if (msg.Value.MessageType == (uint)AuraConfig.Models.AuraMessageType.AudioBands)
                {
                    var payload = AuraShellClient.ExtractPayload<AuraConfig.Models.AudioBandsPayload>(msg.Value);
                    if (payload.Bands != null)
                        AudioBandsReceived?.Invoke(this, payload);
                }
                else if (msg.Value.MessageType == (uint)AuraConfig.Models.AuraMessageType.PerfStats)
                {
                    var p = AuraShellClient.ExtractPayload<AuraConfig.Models.PerfStatsPayload>(msg.Value);
                    PerfStatsReceived?.Invoke(this, p);
                }
            }
        }, ct);
    }

    public void StopAudioStream()
    {
        _audioCts?.Cancel();
        _audioCts?.Dispose();
        _audioCts = null;
    }

    /// <summary>Apply a preset by pushing a ThemeConfig to the service.</summary>
    public async Task<bool> ApplyPresetAsync(GlowPreset preset)
    {
        var theme = new ThemeConfig(preset.Name, preset.Color, preset.AnimSpeedPct);
        return await PushThemeAsync(theme);
    }

    /// <summary>Push taskbar feature toggles to the service.</summary>
    public async Task<bool> SendFeaturesAsync(bool autoHide, bool multiMonitor)
    {
        if (!_client.IsConnected)
        {
            var result = await _client.ConnectAsync(timeoutMs: 1500);
            if (result != AuraShellClient.ConnectResult.Connected)
                return false;
        }
        return await _client.SendFeaturesAsync(autoHide, multiMonitor);
    }

    /// <summary>Push an arbitrary ThemeConfig to the service.</summary>
    public async Task<bool> PushThemeAsync(ThemeConfig theme)
    {
        // Update window border color immediately regardless of service connection state.
        if (Application.Current is App app)
            app.MainWindow?.ApplyBorderColor(
                theme.AccentColor.R, theme.AccentColor.G, theme.AccentColor.B);

        if (!_client.IsConnected)
        {
            var result = await _client.ConnectAsync(timeoutMs: 1500);
            if (result != AuraShellClient.ConnectResult.Connected)
                return false;
        }
        return await _client.PushThemeAsync(theme);
    }

    /// <summary>
    /// Force an immediate reconnection attempt without waiting for the next poll tick.
    /// Safe to call from any thread — marshals onto DispatcherQueue internally via UpdateState.
    /// </summary>
    public Task TriggerReconnectAsync() => PollAsync();

    // ── Notification badge monitor ─────────────────────────────────────────

    private System.Threading.CancellationTokenSource? _notifCts;
    private Windows.UI.Color _badgeColor = Windows.UI.Color.FromArgb(255, 0xFF, 0xA5, 0x00);

    /// <summary>Update the color used for all notification badge glows.</summary>
    public void UpdateBadgeColor(Windows.UI.Color color) => _badgeColor = color;

    /// <summary>
    /// Start polling UserNotificationListener for toast notifications and
    /// applying the badge color to apps with pending notifications. Returns
    /// true if access was granted, false if the user denied notification access.
    /// </summary>
    public async Task<bool> StartNotificationMonitorAsync()
    {
        StopNotificationMonitor();
        try
        {
            var listener = Windows.UI.Notifications.Management.UserNotificationListener.Current;
            var access   = await listener.RequestAccessAsync();
            if (access != Windows.UI.Notifications.Management.UserNotificationListenerAccessStatus.Allowed)
                return false;

            _notifCts = new System.Threading.CancellationTokenSource();
            _ = RunNotificationLoopAsync(listener, _notifCts.Token);
            return true;
        }
        catch
        {
            return false;
        }
    }

    public void StopNotificationMonitor()
    {
        _notifCts?.Cancel();
        _notifCts?.Dispose();
        _notifCts = null;

        // Clear any currently-set badge colors so apps return to their normal color.
        var animator = (Application.Current as App)?.MainWindow?.GlowAnimator;
        if (animator is null) return;
        foreach (var name in _lastNotifApps)
            animator.ClearBadgeColor(name);
        _lastNotifApps.Clear();
    }

    private readonly HashSet<string> _lastNotifApps = new(StringComparer.OrdinalIgnoreCase);

    private async Task RunNotificationLoopAsync(
        Windows.UI.Notifications.Management.UserNotificationListener listener,
        System.Threading.CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var notifs = await listener.GetNotificationsAsync(
                    Windows.UI.Notifications.NotificationKinds.Toast);

                var curr = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (var n in notifs)
                {
                    var name = n.AppInfo?.DisplayInfo?.DisplayName;
                    if (!string.IsNullOrEmpty(name)) curr.Add(name);
                }

                var animator = (Application.Current as App)?.MainWindow?.GlowAnimator;
                if (animator != null)
                {
                    foreach (var app in curr) if (!_lastNotifApps.Contains(app))
                        animator.SetBadgeColor(app, _badgeColor);
                    foreach (var app in _lastNotifApps) if (!curr.Contains(app))
                        animator.ClearBadgeColor(app);
                }
                _lastNotifApps.Clear();
                foreach (var a in curr) _lastNotifApps.Add(a);
            }
            catch { }

            try { await Task.Delay(5000, ct); } catch { return; }
        }
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
