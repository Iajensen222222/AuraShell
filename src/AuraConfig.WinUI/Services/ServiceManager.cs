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

    // ── Audio streaming ────────────────────────────────────────────────────

    /// <summary>Fired on the calling thread when a new AUDIO_BANDS frame arrives.</summary>
    public event EventHandler<AuraConfig.Models.AudioBandsPayload>? AudioBandsReceived;

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
