using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace AuraConfig.Services;

/// <summary>
/// WASAPI loopback capture — reads system audio output directly in-process.
/// No C++ service required. Fires PeakAvailable and BandsAvailable at ~20fps.
/// </summary>
public sealed class LocalAudioCapture : IDisposable
{
    public static readonly LocalAudioCapture Instance = new();

    /// <summary>Fired on a background thread at ~20fps. Value is peak magnitude 0.0–1.0.</summary>
    public event EventHandler<float>?   PeakAvailable;
    /// <summary>Fired on a background thread at ~20fps. Value is 128 log-spaced frequency bands 0.0–1.0.</summary>
    public event EventHandler<float[]>? BandsAvailable;

    public bool IsCapturing { get; private set; }

    private CancellationTokenSource? _cts;

    private const int FftSize = 2048;
    private readonly float[] _fftRing = new float[FftSize];
    private int _fftHead;
    private int _fftFilled;

    private LocalAudioCapture() { }

    public void Start()
    {
        if (_cts is not null) return;
        _cts = new CancellationTokenSource();
        IsCapturing = true;
        Task.Run(() => CaptureLoop(_cts.Token));
    }

    public void Stop()
    {
        _cts?.Cancel();
        _cts?.Dispose();
        _cts = null;
        IsCapturing = false;
    }

    public void Dispose() => Stop();

    // ── Capture loop ───────────────────────────────────────────────────────

    private void CaptureLoop(CancellationToken ct)
    {
        try { CaptureCore(ct); }
        catch (Exception ex) { Logger.Warn("LocalAudioCapture", ex.Message); }
        finally { IsCapturing = false; }
    }

    private void CaptureCore(CancellationToken ct)
    {
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorComObject();

        if (enumerator.GetDefaultAudioEndpoint(0, 1, out var device) < 0) return;

        var acIid = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
        if (device.Activate(ref acIid, 0x17, IntPtr.Zero, out var clientObj) < 0) return;
        var client = (IAudioClient)clientObj;

        if (client.GetMixFormat(out var fmtPtr) < 0) return;
        try
        {
            short tag        = Marshal.ReadInt16(fmtPtr, 0);
            int   nCh        = Marshal.ReadInt16(fmtPtr, 2);
            int   sampleRate = Marshal.ReadInt32(fmtPtr, 4);
            short bps        = Marshal.ReadInt16(fmtPtr, 14);

            bool isFloat = tag == 3;
            if ((ushort)tag == 0xFFFE)
            {
                var sub = new byte[16];
                Marshal.Copy(IntPtr.Add(fmtPtr, 24), sub, 0, 16);
                isFloat = new Guid(sub) == new Guid("00000003-0000-0010-8000-00aa00389b71");
            }

            if (client.Initialize(0, 0x00020000, 5_000_000, 0, fmtPtr, Guid.Empty) < 0) return;

            var ccIid = new Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317");
            if (client.GetService(ref ccIid, out var capObj) < 0) return;
            var capture = (IAudioCaptureClient)capObj;

            client.Start();

            float peak       = 0f;
            long  nextFireMs = Environment.TickCount64 + 50;

            while (!ct.IsCancellationRequested)
            {
                Thread.Sleep(8);

                capture.GetNextPacketSize(out uint pktSize);
                while (pktSize > 0)
                {
                    if (capture.GetBuffer(out var buf, out uint frames, out uint flags, out _, out _) < 0) break;

                    bool silent = (flags & 2) != 0;
                    if (!silent && frames > 0)
                    {
                        int frameCount = (int)frames;
                        for (int f = 0; f < frameCount; f++)
                        {
                            float mono = 0;
                            for (int ch = 0; ch < nCh; ch++)
                            {
                                float s = isFloat
                                    ? Marshal.PtrToStructure<float>(IntPtr.Add(buf, (f * nCh + ch) * 4))
                                    : Marshal.ReadInt16(buf, (f * nCh + ch) * 2) / 32768f;
                                float abs = Math.Abs(s);
                                if (abs > peak) peak = abs;
                                mono += s;
                            }
                            mono /= nCh;
                            _fftRing[_fftHead] = mono;
                            _fftHead = (_fftHead + 1) % FftSize;
                            if (_fftFilled < FftSize) _fftFilled++;
                        }
                    }
                    capture.ReleaseBuffer(frames);
                    capture.GetNextPacketSize(out pktSize);
                }

                long now = Environment.TickCount64;
                if (now >= nextFireMs)
                {
                    PeakAvailable?.Invoke(this, Math.Min(1f, peak));
                    if (_fftFilled >= FftSize)
                        BandsAvailable?.Invoke(this, ComputeBands(sampleRate));
                    peak       = 0f;
                    nextFireMs = now + 50;
                }
            }

            client.Stop();
        }
        finally { Marshal.FreeCoTaskMem(fmtPtr); }
    }

    // ── FFT and frequency band computation ────────────────────────────────

    private float[] ComputeBands(int sampleRate)
    {
        // Snapshot ring buffer in chronological order
        var re  = new float[FftSize];
        var im  = new float[FftSize];
        int start = (_fftHead - FftSize + FftSize) % FftSize;
        for (int i = 0; i < FftSize; i++)
        {
            // Hann window reduces spectral leakage
            float w = 0.5f * (1f - (float)Math.Cos(2.0 * Math.PI * i / (FftSize - 1)));
            re[i] = _fftRing[(start + i) % FftSize] * w;
        }

        Fft(re, im, FftSize);

        // Magnitude of each positive-frequency bin, normalised
        int bins = FftSize / 2;
        var mag  = new float[bins];
        for (int i = 0; i < bins; i++)
            mag[i] = (float)Math.Sqrt(re[i] * re[i] + im[i] * im[i]) / bins;

        // Map bins to 128 logarithmically-spaced bands (20 Hz – 20 kHz)
        var bands   = new float[128];
        double lMin = Math.Log(20.0);
        double lMax = Math.Log(Math.Min(20000.0, sampleRate / 2.0));
        for (int b = 0; b < 128; b++)
        {
            double f0   = Math.Exp(lMin + (lMax - lMin) *  b      / 128.0);
            double f1   = Math.Exp(lMin + (lMax - lMin) * (b + 1) / 128.0);
            int    bin0 = Math.Max(1,        (int)(f0 * FftSize / sampleRate));
            int    bin1 = Math.Min(bins - 1, (int)(f1 * FftSize / sampleRate));
            if (bin1 < bin0) bin1 = bin0;

            float sum = 0;
            for (int i = bin0; i <= bin1; i++) sum += mag[i];
            bands[b] = Math.Min(1.0f, sum / (bin1 - bin0 + 1) * 6f);
        }
        return bands;
    }

    private static void Fft(float[] re, float[] im, int n)
    {
        // Bit-reversal permutation
        for (int i = 1, j = 0; i < n; i++)
        {
            int bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j)
            {
                (re[i], re[j]) = (re[j], re[i]);
                (im[i], im[j]) = (im[j], im[i]);
            }
        }
        // Cooley-Tukey butterfly passes
        for (int len = 2; len <= n; len <<= 1)
        {
            double ang = -2.0 * Math.PI / len;
            float  wRe = (float)Math.Cos(ang);
            float  wIm = (float)Math.Sin(ang);
            for (int i = 0; i < n; i += len)
            {
                float curRe = 1f, curIm = 0f;
                for (int j = 0; j < len / 2; j++)
                {
                    float uRe = re[i + j],         uIm = im[i + j];
                    float vRe = re[i + j + len/2] * curRe - im[i + j + len/2] * curIm;
                    float vIm = re[i + j + len/2] * curIm + im[i + j + len/2] * curRe;
                    re[i + j]         = uRe + vRe;  im[i + j]         = uIm + vIm;
                    re[i + j + len/2] = uRe - vRe;  im[i + j + len/2] = uIm - vIm;
                    float nr = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = nr;
                }
            }
        }
    }

    // ── WASAPI COM interfaces ──────────────────────────────────────────────

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"), ClassInterface(ClassInterfaceType.None)]
    private class MMDeviceEnumeratorComObject { }

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, uint stateMask, out IntPtr devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice ppDevice);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IntPtr ppDevice);
        [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr client);
        [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr client);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, uint clsCtx, IntPtr pParams, [MarshalAs(UnmanagedType.IUnknown)] out object ppv);
        [PreserveSig] int OpenPropertyStore(uint stgmAccess, out IntPtr ppProps);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string ppstrId);
        [PreserveSig] int GetState(out uint pdwState);
    }

    [ComImport, Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioClient
    {
        [PreserveSig] int Initialize(int shareMode, int streamFlags, long hnsBufferDuration, long hnsPeriodicity, IntPtr pFormat, Guid audioSession);
        [PreserveSig] int GetBufferSize(out uint pNumBufferFrames);
        [PreserveSig] int GetStreamLatency(out long phnsLatency);
        [PreserveSig] int GetCurrentPadding(out uint pNumPaddingFrames);
        [PreserveSig] int IsFormatSupported(int shareMode, IntPtr pFormat, out IntPtr ppClosestMatch);
        [PreserveSig] int GetMixFormat(out IntPtr ppDeviceFormat);
        [PreserveSig] int GetDevicePeriod(out long phnsDefault, out long phnsMin);
        [PreserveSig] int Start();
        [PreserveSig] int Stop();
        [PreserveSig] int Reset();
        [PreserveSig] int SetEventHandle(IntPtr eventHandle);
        [PreserveSig] int GetService(ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object ppv);
    }

    [ComImport, Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IAudioCaptureClient
    {
        [PreserveSig] int GetBuffer(out IntPtr ppData, out uint pNumFramesToRead, out uint pdwFlags, out ulong pu64DevicePosition, out ulong pu64QPCPosition);
        [PreserveSig] int ReleaseBuffer(uint numFramesRead);
        [PreserveSig] int GetNextPacketSize(out uint pNumFramesInNextPacket);
    }
}
