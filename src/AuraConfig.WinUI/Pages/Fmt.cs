namespace AuraConfig.Pages;

/// <summary>
/// Value labels shown next to sliders, used from XAML through x:Bind function bindings,
/// e.g. <c>{x:Bind local:Fmt.Px(SizeSlider.Value), Mode=OneWay}</c>.
/// </summary>
public static class Fmt
{
    public static string Px(double value)      => $"{value:0} px";
    public static string Ms(double value)      => $"{value:0} ms";
    public static string Pct(double value)     => $"{value:0}%";
    public static string Seconds(double value) => $"{value:0.#} s";
    /// <summary>Slider value in percent shown as a multiplier, e.g. 150 → "1.5×".</summary>
    public static string Times(double percent) => $"{percent / 100:0.0#}×";
}
