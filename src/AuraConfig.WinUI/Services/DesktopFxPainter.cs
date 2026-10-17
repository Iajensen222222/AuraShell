using static AuraConfig.Services.DesktopIconGlow;

namespace AuraConfig.Services;

/// <summary>
/// Draws the desktop icon hover glow and the click burst into premultiplied BGRA pixel
/// buffers. Shared by <see cref="DesktopIconGlow"/> (layered windows on the desktop) and
/// the settings pages' live previews, so the previews show exactly what the desktop shows.
/// Pure functions of their inputs: safe from any thread.
/// </summary>
internal static class DesktopFxPainter
{
    // ── Glow ───────────────────────────────────────────────────────────────

    const float CycleMinSaturation = 0.8f;   // keeps color cycling vivid even from a pastel base color
    const float CycleMinValue      = 0.8f;
    const float PulseMinLevel      = 0.35f;  // dimmest point of the pulse, as a fraction of full alpha
    const float FlickerMinLevel    = 0.55f;  // dimmest point of the candle flicker
    const float OrbitFloor         = 0.1f;   // brightness of the ring away from the orbiting light
    const float OrbitSharpness     = 3f;     // higher = smaller orbiting light
    const float NeonCoreWidth      = 2.2f;   // px of the bright line at the icon edge
    const float NeonHaloLevel      = 0.55f;  // brightness of the soft glow around the neon line
    const float HaloInnerRatio     = 0.9f;   // round halo starts at this fraction of the icon box half-size
    const float BacklightCenter    = 0.95f;  // backlight brightness behind the middle of the icon
    const float BacklightFalloff   = 1.6f;   // higher = brighter for longer before fading toward the rim
    const float MinPixelAlpha      = 0.004f;

    /// <summary>Everything that decides how one glow bitmap looks.</summary>
    public readonly record struct GlowLook(
        byte R, byte G, byte B,
        float Intensity,          // 0–1
        int Margin,               // px of glow around the icon box
        int CornerRadius,         // icon box corner radius, px
        HoverGlowStyle Style,
        GlowShape Shape,
        float CycleSeconds);

    /// <summary>True when the glow's pixels change over time, so it must be redrawn every frame.</summary>
    public static bool IsAnimatedPaint(HoverGlowStyle style)
        => style is HoverGlowStyle.ColorCycle or HoverGlowStyle.RainbowRing or HoverGlowStyle.Orbit;

    // Position in the current color cycle or pulse, in [0,1).
    private static float Phase(float cycleSeconds, long nowMs)
    {
        long periodMs = Math.Max(1L, (long)(cycleSeconds * 1000f));
        return (nowMs % periodMs) / (float)periodMs;
    }

    /// <summary>
    /// Multiplier on the whole glow's alpha: 1, except Pulse (slow breathing) and
    /// Flicker (an irregular candle-like waver).
    /// </summary>
    public static float GlowLevel(HoverGlowStyle style, float cycleSeconds, long nowMs)
    {
        switch (style)
        {
            case HoverGlowStyle.Pulse:
                float wave = 0.5f * (1f + MathF.Cos(Phase(cycleSeconds, nowMs) * MathF.Tau));
                return PulseMinLevel + (1f - PulseMinLevel) * wave;
            case HoverGlowStyle.Flicker:
                // Three unrelated sines add up to a waver that never visibly repeats.
                float t = nowMs / 1000f;
                float n = 0.5f * MathF.Sin(t * 9.1f) + 0.3f * MathF.Sin(t * 15.7f + 1.3f) + 0.2f * MathF.Sin(t * 27.3f + 2.1f);
                return FlickerMinLevel + (1f - FlickerMinLevel) * (0.5f + 0.5f * n);
            default:
                return 1f;
        }
    }

    /// <summary>The glow's base color at <paramref name="nowMs"/> (Color cycle shifts the hue).</summary>
    public static (byte r, byte g, byte b) GlowColor(in GlowLook look, long nowMs)
    {
        if (look.Style != HoverGlowStyle.ColorCycle) return (look.R, look.G, look.B);
        RgbToHsv(look.R, look.G, look.B, out float h, out float s, out float v);
        return HsvToRgb((h + Phase(look.CycleSeconds, nowMs)) % 1f,
            MathF.Max(s, CycleMinSaturation), MathF.Max(v, CycleMinValue));
    }

    /// <summary>
    /// Paints the glow for an icon box centered in a <paramref name="w"/>×<paramref name="h"/>
    /// bitmap with <see cref="GlowLook.Margin"/> px of room on every side.
    /// </summary>
    public static unsafe void PaintGlow(uint* buf, int w, int h, in GlowLook look, long nowMs)
    {
        new Span<uint>(buf, w * h).Clear();

        float margin    = Math.Max(1, look.Margin);
        float intensity = look.Intensity;
        float cx = w / 2f, cy = h / 2f;
        float hw = cx - margin, hh = cy - margin;   // icon box half-extents
        float cr = Math.Min(look.CornerRadius, Math.Min(hw, hh));
        float boxRadius = Math.Min(hw, hh);
        float phase = Phase(look.CycleSeconds, nowMs);

        var (r, g, b) = GlowColor(look, nowMs);
        float baseHue = 0f, baseSat = 0f, baseVal = 0f;
        bool rainbow = look.Style == HoverGlowStyle.RainbowRing;
        if (rainbow)
        {
            RgbToHsv(look.R, look.G, look.B, out baseHue, out baseSat, out baseVal);
            baseSat = MathF.Max(baseSat, CycleMinSaturation);
            baseVal = MathF.Max(baseVal, CycleMinValue);
        }
        bool orbit = look.Style == HoverGlowStyle.Orbit;
        float orbitAngle = phase * MathF.Tau;

        for (int py = 0; py < h; py++)
        {
            float dy = py + 0.5f - cy;
            for (int px = 0; px < w; px++)
            {
                float dx = px + 0.5f - cx;

                // Rounded-rect signed distance to the icon box: negative inside.
                float qdx = MathF.Abs(dx) - (hw - cr);
                float qdy = MathF.Abs(dy) - (hh - cr);
                float dist = MathF.Sqrt(MathF.Max(qdx, 0) * MathF.Max(qdx, 0)
                                      + MathF.Max(qdy, 0) * MathF.Max(qdy, 0))
                           + MathF.Min(MathF.Max(qdx, qdy), 0) - cr;

                float a = ShapeAlpha(look.Shape, dist, MathF.Sqrt(dx * dx + dy * dy), margin, boxRadius);
                if (a <= 0f) continue;

                byte pr = r, pg = g, pb = b;
                if (rainbow || orbit)
                {
                    // Angle around the icon, 0 at the right, clockwise on screen.
                    float ang = MathF.Atan2(dy, dx);
                    if (rainbow)
                    {
                        (pr, pg, pb) = HsvToRgb((baseHue + ang / MathF.Tau + 1f + phase) % 1f, baseSat, baseVal);
                    }
                    else
                    {
                        float c = 0.5f * (1f + MathF.Cos(ang - orbitAngle));
                        a *= OrbitFloor + (1f - OrbitFloor) * MathF.Pow(c, OrbitSharpness);
                    }
                }

                a *= intensity;
                if (a < MinPixelAlpha) continue;
                buf[py * w + px] = Premultiply(a, pr, pg, pb);
            }
        }
    }

    // Glow strength for one pixel, before intensity and style. dist: signed distance to the
    // icon box (negative inside); radius: distance to the center.
    private static float ShapeAlpha(GlowShape shape, float dist, float radius, float margin, float boxRadius)
    {
        switch (shape)
        {
            case GlowShape.Neon:
            {
                // A crisp bright line hugging the icon box, with a softer glow outside it.
                float core = 1f - MathF.Abs(dist) / NeonCoreWidth;
                float halo = dist > 0f && dist < margin ? NeonHaloLevel * Cube(1f - dist / margin) : 0f;
                return MathF.Max(core, halo);
            }
            case GlowShape.RoundHalo:
            {
                // A circle instead of the icon's square: soft on both sides of a round rim.
                float inner = boxRadius * HaloInnerRatio;
                float d = radius - inner;
                if (d < 0f) return MathF.Max(0f, 1f + d / (boxRadius * 0.35f)) * 0.6f;   // fades out toward the middle
                return d < margin ? Square(1f - d / margin) : 0f;
            }
            case GlowShape.Backlight:
            {
                // A round light behind the icon (the icon is drawn back on top), brightest in
                // the middle and fading out past the icon's edges.
                float t = radius / (boxRadius + margin);
                if (t >= 1f) return 0f;
                return BacklightCenter * MathF.Pow(1f - t * t, BacklightFalloff);
            }
            default:   // SoftEdge: quadratic falloff from the icon box edge, transparent inside
                return dist >= 0f && dist < margin ? Square(1f - dist / margin) : 0f;
        }
    }

    /// <summary>
    /// Draws a premultiplied image over the buffer at (x, y), clipped to the buffer.
    /// Used to put the icon back on top of a Backlight glow.
    /// </summary>
    public static unsafe void DrawOver(uint* buf, int w, int h, uint[] src, int sw, int sh, int x, int y)
    {
        for (int sy = 0; sy < sh; sy++)
        {
            int py = y + sy;
            if (py < 0 || py >= h) continue;
            for (int sx = 0; sx < sw; sx++)
            {
                int px = x + sx;
                if (px < 0 || px >= w) continue;
                uint s = src[sy * sw + sx];
                uint sa = s >> 24;
                if (sa == 0) continue;
                ref uint d = ref buf[py * w + px];
                if (sa == 255) { d = s; continue; }
                uint inv = 255 - sa;
                uint da = (d >> 24) * inv / 255 + sa;
                uint dr = ((d >> 16) & 0xFF) * inv / 255 + ((s >> 16) & 0xFF);
                uint dg = ((d >> 8) & 0xFF) * inv / 255 + ((s >> 8) & 0xFF);
                uint db = (d & 0xFF) * inv / 255 + (s & 0xFF);
                d = (Math.Min(da, 255u) << 24) | (Math.Min(dr, 255u) << 16) | (Math.Min(dg, 255u) << 8) | Math.Min(db, 255u);
            }
        }
    }

    // ── Click burst ────────────────────────────────────────────────────────

    public const int BurstPadding = 8;   // px around the burst radius for spark dots and ring thickness

    const float BurstRingThickness = 3.5f;
    const float BurstSparkRadius   = 3.5f;
    const int   BurstSparkCount    = 8;
    const float BurstSparkReach    = 1.1f;   // sparks travel slightly past the ring
    const float BurstTrailRatio    = 0.75f;  // trailing spark dot position, as a fraction of the lead dot's
    const float BurstFlashPortion  = 0.25f;  // share of the burst during which the center flash shows
    const float BurstFlashRadius   = 6f;
    const int   RippleRings        = 3;
    const float RippleStagger      = 0.2f;   // start delay between ripple rings, as a share of the burst
    const float RippleThickness    = 2.5f;
    const int   RayCount           = 12;
    const float RayStart           = 0.35f;  // rays start this far out, as a share of the reach
    const float RayThickness       = 2.2f;
    const int   ConfettiCount      = 18;
    const float ConfettiGravity    = 0.45f;  // fall over the burst, as a share of the radius
    const float ConfettiHueSpread  = 0.5f;   // confetti hues spread this far around the glow hue
    const int   FireworkCount      = 16;
    const int   FireworkTrail      = 4;      // dots per firework spark streak
    const float FireworkGravity    = 0.3f;
    const float FireworkHueSpread  = 0.15f;

    /// <summary>Side of the square bitmap that holds a burst of this radius.</summary>
    public static int BurstBitmapSize(int radius) => 2 * (radius + BurstPadding);

    /// <summary>
    /// Draws a burst at progress <paramref name="p"/> in [0,1) into a square bitmap of side
    /// <paramref name="size"/>. <paramref name="spin"/> (0–1) varies spark angles between bursts.
    /// </summary>
    public static unsafe void PaintBurst(uint* buf, int size, ClickBurstStyle style, int radius,
        byte r, byte g, byte bl, float p, float spin)
    {
        new Span<uint>(buf, size * size).Clear();

        float c     = size / 2f;
        float eased = 1f - (1f - p) * (1f - p) * (1f - p);   // ease-out cubic
        float fade  = 1f - p;
        float reach = radius * eased;

        if (p < BurstFlashPortion)
        {
            float fp = p / BurstFlashPortion;
            DrawDot(buf, size, c, c, BurstFlashRadius * (1f - fp) + 1f, 1f - fp, r, g, bl);
        }

        switch (style)
        {
            case ClickBurstStyle.Ring:
                DrawRing(buf, size, c, reach, BurstRingThickness * (1f - 0.6f * p) + 1f, fade, r, g, bl);
                break;

            case ClickBurstStyle.Sparks:
                DrawSparks(buf, size, c, reach, p, fade, spin, r, g, bl);
                break;

            case ClickBurstStyle.RingAndSparks:
                DrawRing(buf, size, c, reach, BurstRingThickness * (1f - 0.6f * p) + 1f, fade, r, g, bl);
                DrawSparks(buf, size, c, reach, p, fade, spin, r, g, bl);
                break;

            case ClickBurstStyle.Ripple:
                // Rings that start one after another, like a drop in water.
                for (int k = 0; k < RippleRings; k++)
                {
                    float pk = (p - k * RippleStagger) / (1f - (RippleRings - 1) * RippleStagger);
                    if (pk <= 0f || pk >= 1f) continue;
                    float ek = 1f - (1f - pk) * (1f - pk);
                    DrawRing(buf, size, c, radius * ek, RippleThickness, (1f - pk) * (1f - 0.25f * k), r, g, bl);
                }
                break;

            case ClickBurstStyle.Rays:
                // Straight light rays shooting outward.
                for (int k = 0; k < RayCount; k++)
                {
                    float ang = (k / (float)RayCount + spin) * MathF.Tau;
                    float ux = MathF.Cos(ang), uy = MathF.Sin(ang);
                    float r0 = reach * RayStart + radius * 0.15f * p;
                    float r1 = reach;
                    DrawLine(buf, size, c + ux * r0, c + uy * r0, c + ux * r1, c + uy * r1,
                        RayThickness * (1f - 0.5f * p), fade, r, g, bl);
                }
                break;

            case ClickBurstStyle.Confetti:
            {
                // Colorful bits thrown out that then fall.
                RgbToHsv(r, g, bl, out float hue, out float sat, out float val);
                for (int k = 0; k < ConfettiCount; k++)
                {
                    float rnd  = Hash(k, spin);
                    float ang  = (k / (float)ConfettiCount + spin + 0.3f * rnd) * MathF.Tau;
                    float dist = reach * (0.45f + 0.55f * Hash(k + 37, spin));
                    float x = c + MathF.Cos(ang) * dist;
                    float y = c + MathF.Sin(ang) * dist + radius * ConfettiGravity * p * p;
                    var (cr, cg, cb) = HsvToRgb((hue + (rnd - 0.5f) * 2f * ConfettiHueSpread + 1f) % 1f,
                        MathF.Max(sat, CycleMinSaturation), MathF.Max(val, CycleMinValue));
                    float bit = 2.2f + 1.5f * Hash(k + 91, spin);
                    DrawDot(buf, size, x, y, bit, MathF.Min(1f, fade * 1.4f), cr, cg, cb);
                }
                break;
            }

            case ClickBurstStyle.Firework:
            {
                // Long sparkling streaks that droop and twinkle as they fade.
                RgbToHsv(r, g, bl, out float hue, out float sat, out float val);
                for (int k = 0; k < FireworkCount; k++)
                {
                    float ang  = (k / (float)FireworkCount + spin) * MathF.Tau;
                    float ux = MathF.Cos(ang), uy = MathF.Sin(ang);
                    var (fr, fg, fb) = HsvToRgb((hue + (Hash(k, spin) - 0.5f) * 2f * FireworkHueSpread + 1f) % 1f,
                        MathF.Max(sat, CycleMinSaturation), 1f);
                    float twinkle = p > 0.5f ? 0.6f + 0.4f * MathF.Sin(p * 60f + k * 1.7f) : 1f;
                    for (int t = 0; t < FireworkTrail; t++)
                    {
                        float along = reach * (1f - t * 0.12f);
                        float drop  = radius * FireworkGravity * p * p * (1f - t * 0.15f);
                        float dotR  = (2.4f - t * 0.45f) * (1f - 0.4f * p);
                        DrawDot(buf, size, c + ux * along, c + uy * along + drop, MathF.Max(dotR, 0.6f),
                            fade * twinkle * (1f - t * 0.22f), fr, fg, fb);
                    }
                }
                break;
            }
        }
    }

    private static unsafe void DrawSparks(uint* buf, int size, float c, float reach, float p, float fade,
        float spin, byte r, byte g, byte b)
    {
        float dist = reach * BurstSparkReach;
        float rad  = BurstSparkRadius * (1f - 0.7f * p) + 0.75f;
        for (int k = 0; k < BurstSparkCount; k++)
        {
            float ang = (k / (float)BurstSparkCount + spin) * MathF.Tau;
            float ux = MathF.Cos(ang), uy = MathF.Sin(ang);
            DrawDot(buf, size, c + ux * dist, c + uy * dist, rad, fade, r, g, b);
            // A smaller trailing dot gives each spark a short streak.
            DrawDot(buf, size, c + ux * dist * BurstTrailRatio, c + uy * dist * BurstTrailRatio,
                rad * 0.6f, fade * 0.6f, r, g, b);
        }
    }

    private static unsafe void DrawRing(uint* buf, int size, float c, float radius, float thick, float alpha,
        byte r, byte g, byte b)
    {
        if (alpha <= 0f) return;
        float outer = radius + thick;
        int y0 = Math.Max(0, (int)(c - outer - 1)), y1 = Math.Min(size - 1, (int)(c + outer + 1));
        for (int py = y0; py <= y1; py++)
        {
            float dy = py + 0.5f - c;
            for (int px = y0; px <= y1; px++)
            {
                float dx = px + 0.5f - c;
                float d  = MathF.Sqrt(dx * dx + dy * dy);
                if (d > outer) continue;
                float edge = 1f - MathF.Abs(d - radius) / thick;
                if (edge <= 0f) continue;
                Blend(buf, py * size + px, edge * alpha, r, g, b);
            }
        }
    }

    private static unsafe void DrawLine(uint* buf, int size, float x0, float y0, float x1, float y1,
        float thick, float alpha, byte r, byte g, byte b)
    {
        float half = thick / 2f;
        int minX = Math.Max(0, (int)(MathF.Min(x0, x1) - half - 1)), maxX = Math.Min(size - 1, (int)(MathF.Max(x0, x1) + half + 1));
        int minY = Math.Max(0, (int)(MathF.Min(y0, y1) - half - 1)), maxY = Math.Min(size - 1, (int)(MathF.Max(y0, y1) + half + 1));
        float vx = x1 - x0, vy = y1 - y0;
        float len2 = MathF.Max(vx * vx + vy * vy, 0.0001f);
        for (int py = minY; py <= maxY; py++)
        {
            for (int px = minX; px <= maxX; px++)
            {
                float wx = px + 0.5f - x0, wy = py + 0.5f - y0;
                float t  = Math.Clamp((wx * vx + wy * vy) / len2, 0f, 1f);
                float ex = wx - vx * t, ey = wy - vy * t;
                float cover = half + 0.5f - MathF.Sqrt(ex * ex + ey * ey);
                if (cover <= 0f) continue;
                // Rays are brightest at their outer tip.
                Blend(buf, py * size + px, MathF.Min(cover, 1f) * alpha * (0.35f + 0.65f * t), r, g, b);
            }
        }
    }

    private static unsafe void DrawDot(uint* buf, int size, float cx, float cy, float radius,
        float alpha, byte r, byte g, byte b)
    {
        int x0 = Math.Max(0, (int)(cx - radius - 1)), x1 = Math.Min(size - 1, (int)(cx + radius + 1));
        int y0 = Math.Max(0, (int)(cy - radius - 1)), y1 = Math.Min(size - 1, (int)(cy + radius + 1));
        for (int py = y0; py <= y1; py++)
        {
            float dy = py + 0.5f - cy;
            for (int px = x0; px <= x1; px++)
            {
                float dx = px + 0.5f - cx;
                float cover = radius + 0.5f - MathF.Sqrt(dx * dx + dy * dy);   // 1 px anti-aliased edge
                if (cover <= 0f) continue;
                Blend(buf, py * size + px, MathF.Min(cover, 1f) * alpha, r, g, b);
            }
        }
    }

    // Writes a premultiplied BGRA pixel, keeping whichever of old and new is more opaque.
    private static unsafe void Blend(uint* buf, int idx, float a, byte r, byte g, byte b)
    {
        if (a <= 0f) return;
        if (a > 1f) a = 1f;
        byte a8 = (byte)(a * 255f);
        if (a8 <= (byte)(buf[idx] >> 24)) return;
        buf[idx] = Premultiply(a, r, g, b);
    }

    // ── Helpers ────────────────────────────────────────────────────────────

    // Premultiplied BGRA (uint32 LE: A<<24 | R<<16 | G<<8 | B).
    private static uint Premultiply(float a, byte r, byte g, byte b)
        => (uint)(((byte)(a * 255f) << 24) | ((byte)(r * a) << 16) | ((byte)(g * a) << 8) | (byte)(b * a));

    private static float Square(float x) => x * x;
    private static float Cube(float x) => x * x * x;

    // Repeatable pseudo-random number in [0,1) for particle k of a burst.
    private static float Hash(int k, float spin)
    {
        float v = MathF.Sin(k * 12.9898f + spin * 78.233f) * 43758.547f;
        return v - MathF.Floor(v);
    }

    public static void RgbToHsv(byte r, byte g, byte b, out float h, out float s, out float v)
    {
        float rf = r / 255f, gf = g / 255f, bf = b / 255f;
        float max = MathF.Max(rf, MathF.Max(gf, bf));
        float min = MathF.Min(rf, MathF.Min(gf, bf));
        float d   = max - min;
        v = max;
        s = max <= 0f ? 0f : d / max;
        if (d <= 0f) { h = 0f; return; }
        if (max == rf)      h = (gf - bf) / d;
        else if (max == gf) h = (bf - rf) / d + 2f;
        else                h = (rf - gf) / d + 4f;
        h /= 6f;
        if (h < 0f) h += 1f;
    }

    public static (byte r, byte g, byte b) HsvToRgb(float h, float s, float v)
    {
        float c  = v * s;
        float hp = h * 6f;
        float x  = c * (1f - MathF.Abs(hp % 2f - 1f));
        float m  = v - c;
        var (r, g, b) = (int)hp switch
        {
            0 => (c, x, 0f),
            1 => (x, c, 0f),
            2 => (0f, c, x),
            3 => (0f, x, c),
            4 => (x, 0f, c),
            _ => (c, 0f, x),
        };
        return ((byte)((r + m) * 255f), (byte)((g + m) * 255f), (byte)((b + m) * 255f));
    }
}
