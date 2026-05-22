using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace AuraConfig.Pages;

public sealed partial class ThemesPage : Page
{
    private AuraTheme? _selected;
    private bool _suppressHandlers;

    public ThemesPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ThemeStore.Instance.CustomThemes.CollectionChanged += (_, _) => RebuildList();
        RebuildList();
    }

    // ── List builder ───────────────────────────────────────────────────────

    private void RebuildList()
    {
        ThemeListPanel.Children.Clear();

        AddSectionHeader("BUILT-IN");
        foreach (var t in ThemeStore.Instance.BuiltInThemes)
            ThemeListPanel.Children.Add(MakeThemeCard(t));

        if (ThemeStore.Instance.CustomThemes.Count > 0)
        {
            AddSectionHeader("MY THEMES");
            foreach (var t in ThemeStore.Instance.CustomThemes)
                ThemeListPanel.Children.Add(MakeThemeCard(t));
        }
    }

    private void AddSectionHeader(string text)
    {
        ThemeListPanel.Children.Add(new TextBlock
        {
            Text             = text,
            Style            = (Style)Application.Current.Resources["CaptionTextBlockStyle"],
            Foreground       = (Brush)Application.Current.Resources["AccentTextFillColorPrimaryBrush"],
            Margin           = new Thickness(8, 8, 8, 2),
            CharacterSpacing = 50,
            FontWeight       = Microsoft.UI.Text.FontWeights.SemiBold,
        });
    }

    private Button MakeThemeCard(AuraTheme theme)
    {
        var swatch = new Border
        {
            Width        = 16,
            Height       = 16,
            CornerRadius = new CornerRadius(3),
            Background   = new SolidColorBrush(theme.Color),
        };

        var nameBlock = new TextBlock
        {
            Text              = theme.Name,
            FontSize          = 13,
            VerticalAlignment = VerticalAlignment.Center,
        };

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        row.Children.Add(swatch);
        row.Children.Add(nameBlock);

        var btn = new Button
        {
            Content                    = row,
            Tag                        = theme,
            HorizontalAlignment        = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Left,
            Padding                    = new Thickness(8, 6, 12, 6),
            CornerRadius               = new CornerRadius(4),
            Background                 = new SolidColorBrush(Colors.Transparent),
            BorderThickness            = new Thickness(0),
        };
        btn.Click += ThemeCard_Click;
        return btn;
    }

    private void ThemeCard_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: AuraTheme theme }) return;
        SelectTheme(theme);
    }

    // ── Selection / editor ─────────────────────────────────────────────────

    private void SelectTheme(AuraTheme theme)
    {
        _selected = theme;
        HighlightSelected(theme);
        PopulateEditor(theme);
    }

    private void HighlightSelected(AuraTheme target)
    {
        foreach (var child in ThemeListPanel.Children)
        {
            if (child is not Button btn) continue;
            bool isTarget = btn.Tag is AuraTheme t && ReferenceEquals(t, target);
            btn.Background = isTarget
                ? new SolidColorBrush(Color.FromArgb(0x20, 0xFF, 0xFF, 0xFF))
                : new SolidColorBrush(Colors.Transparent);
        }
    }

    private void PopulateEditor(AuraTheme theme)
    {
        _suppressHandlers = true;

        NoSelectionPanel.Visibility = Visibility.Collapsed;
        EditorPanel.Visibility      = Visibility.Visible;

        BuiltInNoticeBorder.Visibility = theme.IsBuiltIn ? Visibility.Visible : Visibility.Collapsed;
        SaveBtn.Visibility             = theme.IsBuiltIn ? Visibility.Collapsed : Visibility.Visible;
        DeleteBtn.Visibility           = theme.IsBuiltIn ? Visibility.Collapsed : Visibility.Visible;

        NameInput.Text         = theme.Name;
        NameInput.IsReadOnly   = theme.IsBuiltIn;
        ColorHexInput.Text     = theme.HexColor;
        ColorHexInput.IsReadOnly = theme.IsBuiltIn;
        SpeedSlider.Value      = theme.AnimSpeedPct;
        SpeedSlider.IsEnabled  = !theme.IsBuiltIn;
        HoverToggle.IsOn       = theme.ShowOnHover;
        HoverToggle.IsEnabled  = !theme.IsBuiltIn;
        GlowToggle.IsOn        = theme.GlowEnabled;
        GlowToggle.IsEnabled   = !theme.IsBuiltIn;

        VisualizerToggle.IsOn              = theme.VisualizerEnabled;
        VisualizerToggle.IsEnabled         = !theme.IsBuiltIn;
        VisualizerHeightSlider.Value       = theme.VisualizerHeightPx;
        VisualizerHeightSlider.IsEnabled   = !theme.IsBuiltIn;
        VisualizerBrightnessSlider.Value   = theme.VisualizerBrightness * 100.0;
        VisualizerBrightnessSlider.IsEnabled = !theme.IsBuiltIn;

        UpdateColorSwatch(theme.Color);
        StatusBanner.Visibility = Visibility.Collapsed;

        _suppressHandlers = false;
    }

    private void UpdateColorSwatch(Color c)
    {
        ColorSwatch.Background = new SolidColorBrush(c);
    }

    // ── Editor handlers ────────────────────────────────────────────────────

    private void NameInput_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.Name = NameInput.Text;
        RebuildList();
        HighlightSelected(_selected);
    }

    private void ColorHexInput_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter)
            ApplyColor_Click(sender, null!);
    }

    private void ApplyColor_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null || _selected.IsBuiltIn) return;

        var raw = ColorHexInput.Text.Trim().TrimStart('#');
        if (raw.Length != 6) return;
        try
        {
            byte r = Convert.ToByte(raw[0..2], 16);
            byte g = Convert.ToByte(raw[2..4], 16);
            byte b = Convert.ToByte(raw[4..6], 16);
            _selected.R = r; _selected.G = g; _selected.B = b;
            UpdateColorSwatch(_selected.Color);
            RebuildList();
            HighlightSelected(_selected);
        }
        catch { }
    }

    private void SpeedSlider_ValueChanged(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.AnimSpeedPct = (int)e.NewValue;
    }

    private void HoverToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.ShowOnHover = HoverToggle.IsOn;
    }

    private void GlowToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.GlowEnabled = GlowToggle.IsOn;
    }

    private void VisualizerToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.VisualizerEnabled = VisualizerToggle.IsOn;
    }

    private void VisualizerHeightSlider_ValueChanged(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.VisualizerHeightPx = (int)e.NewValue;
    }

    private void VisualizerBrightnessSlider_ValueChanged(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (_suppressHandlers || _selected is null || _selected.IsBuiltIn) return;
        _selected.VisualizerBrightness = e.NewValue / 100.0;
    }

    // ── Action buttons ─────────────────────────────────────────────────────

    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        await ServiceManager.Instance.PushThemeAsync(_selected.ToThemeConfig());
        ShowStatus($"✓  {_selected.Name} applied");
    }

    private void Duplicate_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        var copy = ThemeStore.Instance.Duplicate(_selected);
        // RebuildList() fires via CollectionChanged; select the copy
        SelectTheme(copy);
        ShowStatus($"Created “{copy.Name}” — edit and Save to keep changes");
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null || _selected.IsBuiltIn) return;
        ThemeStore.Instance.UpdateCustom(_selected);
        ShowStatus($"✓  “{_selected.Name}” saved");
    }

    private async void Delete_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null || _selected.IsBuiltIn) return;

        var dlg = new ContentDialog
        {
            XamlRoot          = XamlRoot,
            Title             = $"Delete “{_selected.Name}”?",
            Content           = "This theme will be permanently removed.",
            PrimaryButtonText = "Delete",
            CloseButtonText   = "Cancel",
        };

        if (await dlg.ShowAsync() != ContentDialogResult.Primary) return;

        ThemeStore.Instance.DeleteCustom(_selected);
        _selected = null;
        EditorPanel.Visibility      = Visibility.Collapsed;
        NoSelectionPanel.Visibility = Visibility.Visible;
    }

    private void NewTheme_Click(object sender, RoutedEventArgs e)
    {
        var theme = ThemeStore.Instance.AddCustom(new AuraTheme
        {
            Name = $"My Theme {ThemeStore.Instance.CustomThemes.Count + 1}",
            R = 0, G = 120, B = 212,
        });
        SelectTheme(theme);
    }

    // ── Status banner ──────────────────────────────────────────────────────

    private async void ShowStatus(string message)
    {
        StatusText.Text         = message;
        StatusBanner.Visibility = Visibility.Visible;
        await Task.Delay(3000);
        StatusBanner.Visibility = Visibility.Collapsed;
    }
}
