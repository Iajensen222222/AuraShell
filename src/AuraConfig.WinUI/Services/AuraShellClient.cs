using System.IO.Pipes;
using System.Runtime.InteropServices;
using AuraConfig.Models;

namespace AuraConfig.Services;

/// <summary>
/// Named-pipe IPC client — C# port of the C++ AppClient.
/// Connects to \\.\pipe\AuraShell_Control and speaks the 2064-byte binary protocol.
/// Thread safety: call from a single thread (UI or background worker).
/// </summary>
public sealed class AuraShellClient : IDisposable
{
    private const string PipeName     = "AuraShell_Control";
    private const int    FrameSize    = AuraMessage.FrameSize;
    private const uint   ClientVersion = 0x0500; // v5.0 (this app)

    private NamedPipeClientStream? _pipe;
    private uint _seq = 1;
    private bool _disposed;

    public bool IsConnected => _pipe?.IsConnected ?? false;

    // ── Connection ─────────────────────────────────────────────────────────

    public enum ConnectResult { Connected, ServiceNotRunning, AccessDenied, Timeout, Unknown }

    public async Task<ConnectResult> ConnectAsync(int timeoutMs = 2000,
                                                   CancellationToken ct = default)
    {
        if (IsConnected) return ConnectResult.Connected;

        try
        {
            _pipe = new NamedPipeClientStream(".", PipeName,
                PipeDirection.InOut, PipeOptions.None);

            await _pipe.ConnectAsync(timeoutMs, ct).ConfigureAwait(false);

            // Perform HANDSHAKE_REQUEST / HANDSHAKE_RESPONSE
            var req = AuraMessage.Create(AuraMessageType.HandshakeRequest, _seq++);
            var hs  = new HandshakePayload
            {
                ClientPID     = (uint)Environment.ProcessId,
                ClientVersion = ClientVersion,
                Capabilities  = 0x00FF
            };
            SetPayload(ref req, hs);

            await SendAsync(req, ct).ConfigureAwait(false);
            var resp = await ReceiveAsync(ct).ConfigureAwait(false);

            return resp.IsValid
                ? ConnectResult.Connected
                : ConnectResult.Unknown;
        }
        catch (IOException)           { return ConnectResult.ServiceNotRunning; }
        catch (UnauthorizedAccessException) { return ConnectResult.AccessDenied; }
        catch (TimeoutException)      { return ConnectResult.Timeout; }
        catch                         { return ConnectResult.Unknown; }
    }

    public void Disconnect()
    {
        _pipe?.Dispose();
        _pipe = null;
    }

    // ── IPC operations ─────────────────────────────────────────────────────

    public async Task<ServiceState?> QueryStateAsync(CancellationToken ct = default)
    {
        if (!IsConnected) return null;
        try
        {
            var req = AuraMessage.Create(AuraMessageType.QueryState, _seq++);
            await SendAsync(req, ct).ConfigureAwait(false);
            var resp = await ReceiveAsync(ct).ConfigureAwait(false);

            if (!resp.IsValid) return null;
            var qr = GetPayload<QueryStateResponse>(resp);

            return new ServiceState(
                connected: true,
                theme:     qr.CurrentTheme ?? "—",
                uptime:    qr.UptimeSeconds,
                version:   qr.ServiceVersion,
                elevated:  qr.IsElevated
            );
        }
        catch { return null; }
    }

    public async Task<bool> PushThemeAsync(ThemeConfig theme,
                                            CancellationToken ct = default)
    {
        if (!IsConnected) return false;
        try
        {
            var req = AuraMessage.Create(AuraMessageType.PushTheme, _seq++);
            var tp  = new ThemePayload
            {
                ThemeName        = theme.Name,
                ColorCount       = 0,
                AnimationSpeedPct = (uint)theme.AnimSpeedPct
            };
            SetPayload(ref req, tp);
            await SendAsync(req, ct).ConfigureAwait(false);
            var ack = await ReceiveAsync(ct).ConfigureAwait(false);
            return ack.MessageType == (uint)AuraMessageType.Ack;
        }
        catch { return false; }
    }

    // ── Unsolicited message reading ────────────────────────────────────────

    /// <summary>
    /// Reads the next raw message frame from the pipe without sending a request first.
    /// Used by the audio stream background reader. Returns null if the pipe is closed.
    /// </summary>
    public async Task<AuraMessage?> ReadNextMessageAsync(CancellationToken ct)
    {
        if (!IsConnected) return null;
        try { return await ReceiveAsync(ct).ConfigureAwait(false); }
        catch { return null; }
    }

    // ── Binary framing helpers ─────────────────────────────────────────────

    private async Task SendAsync(AuraMessage msg, CancellationToken ct)
    {
        var buf = StructToBytes(msg);
        await _pipe!.WriteAsync(buf.AsMemory(0, FrameSize), ct).ConfigureAwait(false);
        await _pipe.FlushAsync(ct).ConfigureAwait(false);
    }

    private async Task<AuraMessage> ReceiveAsync(CancellationToken ct)
    {
        var buf  = new byte[FrameSize];
        int read = 0;
        while (read < FrameSize)
        {
            int n = await _pipe!.ReadAsync(buf.AsMemory(read, FrameSize - read), ct)
                                 .ConfigureAwait(false);
            if (n == 0) throw new IOException("Pipe closed");
            read += n;
        }
        return BytesToStruct<AuraMessage>(buf);
    }

    private static void SetPayload<T>(ref AuraMessage msg, T payload) where T : struct
    {
        int size = Marshal.SizeOf<T>();
        byte[] bytes = StructToBytes(payload);
        Array.Copy(bytes, msg.Payload, size);
        msg.PayloadSize = (uint)size;
    }

    private static T GetPayload<T>(AuraMessage msg) where T : struct =>
        BytesToStruct<T>(msg.Payload);

    /// <summary>Extracts a typed payload from a received message frame.</summary>
    public static T ExtractPayload<T>(AuraMessage msg) where T : struct =>
        BytesToStruct<T>(msg.Payload);

    private static byte[] StructToBytes<T>(T s) where T : struct
    {
        int  size = Marshal.SizeOf<T>();
        var  buf  = new byte[size];
        var  ptr  = Marshal.AllocHGlobal(size);
        try
        {
            Marshal.StructureToPtr(s, ptr, false);
            Marshal.Copy(ptr, buf, 0, size);
            return buf;
        }
        finally { Marshal.FreeHGlobal(ptr); }
    }

    private static T BytesToStruct<T>(byte[] buf) where T : struct
    {
        var ptr = Marshal.AllocHGlobal(buf.Length);
        try
        {
            Marshal.Copy(buf, 0, ptr, buf.Length);
            return Marshal.PtrToStructure<T>(ptr);
        }
        finally { Marshal.FreeHGlobal(ptr); }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Disconnect();
    }
}
