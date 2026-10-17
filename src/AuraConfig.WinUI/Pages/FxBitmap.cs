using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Microsoft.UI.Xaml.Media.Imaging;

namespace AuraConfig.Pages;

/// <summary>
/// A WriteableBitmap plus the pixel buffer DesktopFxPainter draws into, for the Desktop FX
/// previews. Both use premultiplied BGRA, so pixels copy straight across.
/// </summary>
internal sealed class FxBitmap
{
    public WriteableBitmap Bitmap { get; }
    public uint[] Pixels { get; }
    public int Size { get; }

    public FxBitmap(int size)
    {
        Size   = size;
        Bitmap = new WriteableBitmap(size, size);
        Pixels = new uint[size * size];
    }

    /// <summary>Copies <see cref="Pixels"/> to the bitmap and redraws it.</summary>
    public void Publish()
    {
        using (var stream = Bitmap.PixelBuffer.AsStream())
            stream.Write(MemoryMarshal.AsBytes(Pixels.AsSpan()));
        Bitmap.Invalidate();
    }
}
