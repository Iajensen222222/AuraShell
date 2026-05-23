using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using AuraConfig.Pages;
using AuraConfig.Services;
using Windows.Graphics;
using AuraConfig.Models;

namespace AuraConfig;

public sealed partial class MainWindow : Window
{
    private Frame _navFrame = new Frame();
    private readonly GlowAnimator _glowAnimator = new();
    private readonly TrayIconService _tray = new();
    private bool _quitting = false;

    /// <summary>Exposed so pages (VisualsPage, AppColorsPage) can call SetColor/SetAppColor etc.</summary>
    public GlowAnimator GlowAnimator => _glowAnimator;

    // Audio-reactive border: enabled when service is connected and streaming audio
    private bool _audioReactiveBorder = false;
    public bool AudioReactiveBorderEnabled
    {
        get => _audioReactiveBorder;
        set => _audioReactiveBorder = value;
    }

    // Accent orange — matches the original NavigationView selection indicator.
    private static readonly Windows.UI.Color AccentOrange =
        Windows.UI.Color.FromArgb(0xFF, 0xFF, 0x8C, 0x00);

    public MainWindow()
    {
        InitializeComponent();
        Content = BuildShell();
        _navFrame.Navigate(typeof(DashboardPage));
        SetWindowSizeAndCenter(1100, 780);
        ServiceManager.Instance.StartPolling(DispatcherQueue);

        // Wire audio band data → animated window border pulse
        ServiceManager.Instance.AudioBandsReceived += OnAudioBandsReceived;

        // Restore last-applied color if available, else default blue. Either way,
        // the call happens after BuildShell so SetWindowHandle has already wired
        // up the app's own HWND.
        var lastTheme = ServiceManager.Instance.LastAppliedTheme;
        if (lastTheme is not null)
        {
            var c = lastTheme.AccentColor;
            Logger.Info("MainWindow", $"Restoring border from saved theme '{lastTheme.Name}' #{c.R:X2}{c.G:X2}{c.B:X2}");
            _glowAnimator.SetColor(c.R, c.G, c.B);
        }
        else
        {
            _glowAnimator.SetColor(0x00, 0x78, 0xD4);
        }

        // Register the foreground hook from the UI thread so EVENT_SYSTEM_FOREGROUND
        // events arrive on a thread with a message pump. Has to happen after
        // SetColor() so the GetEffectiveColor() fallback inside the callback
        // sees the right _themeColor.
        _glowAnimator.StartForegroundHook();

        // Set taskbar / titlebar icon
        try
        {
            string iconPath = System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcon.ico");
            if (System.IO.File.Exists(iconPath))
                AppWindow.SetIcon(iconPath);
        }
        catch { }

        // Tray icon + minimize-to-tray on close so the GlowAnimator's repaint
        // loop keeps the borders applied even when the user clicks the X.
        // Quit only via tray context menu's 'Quit' item.
        _tray.Initialize("AuraShell — click to restore window");
        _tray.ShowRequested += () => DispatcherQueue.TryEnqueue(() =>
        {
            AppWindow.Show();
            this.Activate();
        });
        _tray.QuitRequested += () => DispatcherQueue.TryEnqueue(() =>
        {
            Logger.Info("MainWindow", "Quit requested from tray");
            _quitting = true;
            _tray.Dispose();
            this.Close();
        });

        AppWindow.Closing += (s, args) =>
        {
            if (_quitting) return;          // user picked Quit from tray menu
            args.Cancel = true;
            Logger.Info("MainWindow", "Close intercepted → hiding to tray");
            AppWindow.Hide();
        };
    }

    // ── Window geometry ────────────────────────────────────────────────────

    private void SetWindowSizeAndCenter(int width, int height)
    {
        var appWindow = AppWindow;
        appWindow.Resize(new SizeInt32(width, height));

        var displayArea = DisplayArea.GetFromWindowId(appWindow.Id, DisplayAreaFallback.Primary);
        var workArea = displayArea.WorkArea;

        var x = workArea.X + (workArea.Width  - width)  / 2;
        var y = workArea.Y + (workArea.Height - height) / 2;
        appWindow.Move(new PointInt32(x, y));
    }

    // ── Navigation shell ───────────────────────────────────────────────────
    // SplitView used instead of NavigationView to avoid a WinUI 3 2.0.1 crash
    // (STATUS_UNHANDLED_EXCEPTION at offset 0x3ac82d in Microsoft.UI.Xaml.dll)
    // when NavigationView Content is set in unpackaged apps with custom
    // Application.Resources ThemeDictionaries.

    private UIElement BuildShell()
    {
        var pane = BuildPane();
        var splitView = new SplitView
        {
            DisplayMode       = SplitViewDisplayMode.Inline,
            IsPaneOpen        = true,
            OpenPaneLength    = 240,
            CompactPaneLength = 48,
            Pane              = pane,
            Content           = _navFrame,
        };
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        _glowAnimator.SetWindowHandle(hwnd);

        return splitView;
    }

    private UIElement BuildPane()
    {
        var grid = new Grid();
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        // ── Title ─────────────────────────────────────────────────────────
        var title = new TextBlock
        {
            Text       = "AuraShell",
            FontSize   = 14,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            FontFamily = new FontFamily("Segoe UI Variable Display"),
            Margin     = new Thickness(20, 16, 20, 8),
        };
        Grid.SetRow(title, 0);
        grid.Children.Add(title);

        // ── Main nav items ─────────────────────────────────────────────────
        var mainItems = new StackPanel { Spacing = 2, Margin = new Thickness(8, 4, 8, 4) };
        mainItems.Children.Add(MakeNavItem("", "Dashboard",     "dashboard"));
        mainItems.Children.Add(MakeNavItem("", "Visuals",       "visuals"));
        mainItems.Children.Add(MakeNavItem("", "Themes",        "themes"));
        mainItems.Children.Add(MakeNavItem("", "App Colors",    "appcolors"));
        mainItems.Children.Add(MakeNavItem("", "Behavior",      "behavior"));
        mainItems.Children.Add(MakeNavItem("", "Desktop Items", "desktop"));
        Grid.SetRow(mainItems, 1);
        grid.Children.Add(mainItems);

        // ── Footer nav items ───────────────────────────────────────────────
        var footer = new StackPanel { Spacing = 2, Margin = new Thickness(8, 4, 8, 12) };
        footer.Children.Add(MakeNavItem("", "Schedule", "schedule"));
        footer.Children.Add(MakeNavItem("", "About",    "about"));
        footer.Children.Add(MakeNavItem("", "Settings", "settings"));
        Grid.SetRow(footer, 2);
        grid.Children.Add(footer);

        _paneMainItems   = mainItems;
        _paneFooterItems = footer;

        // Select Dashboard as default
        SelectNavItem(mainItems, "dashboard");

        return grid;
    }

    private StackPanel? _paneMainItems;
    private StackPanel? _paneFooterItems;
    private string _currentTag = "dashboard";

    private Button MakeNavItem(string glyph, string label, string tag)
    {
        // Left selection indicator (3 × 20 px orange pill).
        var indicator = new Rectangle
        {
            Width           = 3,
            Height          = 20,
            RadiusX         = 1.5,
            RadiusY         = 1.5,
            Fill            = new SolidColorBrush(Colors.Transparent),
            VerticalAlignment = VerticalAlignment.Center,
            Margin          = new Thickness(0, 0, 10, 0),
            Tag             = "indicator",
        };

        var icon = new FontIcon { Glyph = glyph, FontSize = 16, Width = 20 };
        var text = new TextBlock
        {
            Text              = label,
            FontSize          = 13,
            VerticalAlignment = VerticalAlignment.Center,
        };

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10 };
        row.Children.Add(icon);
        row.Children.Add(text);

        // Grid: col0 = indicator pill, col1 = icon+label
        var itemGrid = new Grid();
        itemGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        itemGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        Grid.SetColumn(indicator, 0);
        Grid.SetColumn(row, 1);
        itemGrid.Children.Add(indicator);
        itemGrid.Children.Add(row);

        var btn = new Button
        {
            Content                    = itemGrid,
            Tag                        = tag,
            HorizontalAlignment        = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Left,
            Padding                    = new Thickness(8, 8, 12, 8),
            CornerRadius               = new CornerRadius(4),
            Background                 = new SolidColorBrush(Colors.Transparent),
            BorderThickness            = new Thickness(0),
        };
        btn.Click += NavItem_Click;
        return btn;
    }

    private void SelectNavItem(StackPanel panel, string tag)
    {
        foreach (var child in panel.Children)
        {
            if (child is not Button btn || btn.Tag is not string btnTag) continue;

            bool selected = btnTag == tag;

            btn.Background = selected
                ? new SolidColorBrush(Windows.UI.Color.FromArgb(0x18, 0xFF, 0xFF, 0xFF))
                : new SolidColorBrush(Colors.Transparent);

            // Update the selection indicator pill
            if (btn.Content is Grid g)
            {
                foreach (var el in g.Children)
                {
                    if (el is Rectangle rect && rect.Tag is "indicator")
                    {
                        rect.Fill = selected
                            ? new SolidColorBrush(AccentOrange)
                            : new SolidColorBrush(Colors.Transparent);
                        break;
                    }
                }
            }
        }
    }

    private void NavItem_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn || btn.Tag is not string tag) return;

        _currentTag = tag;
        Logger.Info("Nav", $"Click → {tag}");

        if (_paneMainItems   is not null) SelectNavItem(_paneMainItems,   tag);
        if (_paneFooterItems is not null) SelectNavItem(_paneFooterItems, tag);

        Type? pageType = tag switch
        {
            "dashboard" => typeof(DashboardPage),
            "visuals"   => typeof(VisualsPage),
            "themes"    => typeof(ThemesPage),
            "appcolors" => typeof(AppColorsPage),
            "behavior"  => typeof(BehaviorPage),
            "desktop"   => typeof(DesktopItemsPage),
            "schedule"  => typeof(SchedulePage),
            "about"     => typeof(AboutPage),
            "settings"  => typeof(SettingsPage),
            _           => null
        };

        if (pageType is not null)
        {
            try { _navFrame.Navigate(pageType); }
            catch (Exception ex)
            {
                Logger.Error("Nav", $"Navigate({pageType.Name}) failed", ex);
                throw;
            }
        }
    }

    // ── Audio-reactive border pulse ────────────────────────────────────────

    private void OnAudioBandsReceived(object? sender, AudioBandsPayload payload)
    {
        // Only pulse when the feature is enabled; avoid thrashing DWM at 10fps otherwise
        if (!_audioReactiveBorder) return;
        _glowAnimator.Pulse(payload.Peak);
    }

    // ── Theme apply → border color ─────────────────────────────────────────

    /// <summary>
    /// Called by any page that applies a theme to immediately update all window
    /// borders without waiting for a service round-trip.
    /// </summary>
    public void ApplyBorderColor(byte r, byte g, byte b)
        => _glowAnimator.SetColor(r, g, b);
}
