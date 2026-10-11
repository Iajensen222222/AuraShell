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
    private readonly GlowAnimator    _glowAnimator    = new();
    private readonly DesktopIconGlow _desktopIconGlow = new();
    private readonly TrayIconService _tray = new();
    private bool _quitting = false;

    /// <summary>Exposed so pages (VisualsPage, AppColorsPage) can call SetColor/SetAppColor etc.</summary>
    public GlowAnimator    GlowAnimator    => _glowAnimator;
    public DesktopIconGlow DesktopIconGlow => _desktopIconGlow;

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

        // Apply saved theme (and sync plain App.xaml brushes) before first page renders.
        ThemeService.ThemeChanged += ApplyCaptionButtonTheme;
        ThemeService.Apply(AppSettings.Get("AppTheme") ?? "Dark", Content as FrameworkElement);

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

        // Desktop icon hover glow — runs on its own background thread.
        // Sync glow color with whatever theme was just restored above.
        {
            var (dr, dg, db) = lastTheme is not null
                ? (lastTheme.AccentColor.R, lastTheme.AccentColor.G, lastTheme.AccentColor.B)
                : ((byte)0x00, (byte)0x78, (byte)0xD4);
            _desktopIconGlow.SetColor(dr, dg, db);
        }
        // Saved Desktop FX settings (color, glow style, click burst…) override the defaults.
        DesktopFxSettings.ApplyTo(_desktopIconGlow);
        _desktopIconGlow.Start();

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
            _desktopIconGlow.Start();
        });
        _tray.QuitRequested += () => DispatcherQueue.TryEnqueue(() =>
        {
            Logger.Info("MainWindow", "Quit requested from tray");
            _quitting = true;
            _desktopIconGlow.Dispose();
            _tray.Dispose();
            this.Close();
        });

        AppWindow.Closing += (s, args) =>
        {
            if (_quitting) return;          // user picked Quit from tray menu
            args.Cancel = true;
            Logger.Info("MainWindow", "Close intercepted → hiding to tray");
            _desktopIconGlow.Stop();        // restores Explorer's native hover before hiding
            AppWindow.Hide();
        };

        // If a second instance launches while we're in the tray, it signals this
        // named event. We watch on a thread-pool thread and marshal to the UI thread.
        System.Threading.ThreadPool.QueueUserWorkItem(_ =>
        {
            var evt = App.ShowWindowEvent;
            if (evt is null) return;
            while (true)
            {
                evt.WaitOne();
                DispatcherQueue.TryEnqueue(() =>
                {
                    AppWindow.Show();
                    this.Activate();
                    // Closing to the tray stopped the desktop effects; restart them like the tray's Show does.
                    _desktopIconGlow.Start();
                });
            }
        });
    }

    // ── Window geometry ────────────────────────────────────────────────────

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr hwnd);

    private const double DefaultDpi       = 96.0;
    private const double MaxWorkAreaShare = 0.92;   // never open larger than this share of the screen

    /// <summary>
    /// Sizes the window in DIPs (so it looks the same at 100 % and 150 % scaling),
    /// capped to the monitor's work area, and centers it.
    /// </summary>
    private void SetWindowSizeAndCenter(int widthDip, int heightDip)
    {
        var appWindow = AppWindow;
        var displayArea = DisplayArea.GetFromWindowId(appWindow.Id, DisplayAreaFallback.Primary);
        var workArea = displayArea.WorkArea;

        double scale = 1.0;
        try
        {
            uint dpi = GetDpiForWindow(WinRT.Interop.WindowNative.GetWindowHandle(this));
            if (dpi > 0) scale = dpi / DefaultDpi;
        }
        catch { }

        int width  = Math.Min((int)(widthDip  * scale), (int)(workArea.Width  * MaxWorkAreaShare));
        int height = Math.Min((int)(heightDip * scale), (int)(workArea.Height * MaxWorkAreaShare));
        appWindow.Resize(new SizeInt32(width, height));

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
            PaneBackground    = new SolidColorBrush(Colors.Transparent),
            Pane              = pane,
            Content           = _navFrame,
        };
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        _glowAnimator.SetWindowHandle(hwnd);
        HotkeyService.Instance.Initialize(hwnd, DispatcherQueue);

        // Extend content into titlebar (Windows Settings style: no visible titlebar strip,
        // caption buttons float over the top-right corner of the content).
        var tb = AppWindow.TitleBar;
        tb.ExtendsContentIntoTitleBar = true;
        tb.ButtonBackgroundColor         = Colors.Transparent;
        tb.ButtonInactiveBackgroundColor = Colors.Transparent;
        tb.ButtonForegroundColor         = Colors.White;
        tb.ButtonInactiveForegroundColor = Windows.UI.Color.FromArgb(0xFF, 0x9D, 0x9D, 0x9D);
        tb.ButtonHoverBackgroundColor    = Windows.UI.Color.FromArgb(0x20, 0xFF, 0xFF, 0xFF);
        tb.ButtonPressedBackgroundColor  = Windows.UI.Color.FromArgb(0x40, 0xFF, 0xFF, 0xFF);

        // Push frame content below the caption-button zone so nothing is hidden behind them.
        _navFrame.Margin = new Thickness(0, 48, 0, 0);

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
            Margin     = new Thickness(20, 52, 20, 8),
        };
        Grid.SetRow(title, 0);
        grid.Children.Add(title);

        // ── Main nav items ─────────────────────────────────────────────────
        var mainItems = new StackPanel { Spacing = 2, Margin = new Thickness(8, 4, 8, 4) };
        mainItems.Children.Add(MakeNavItem("\uE80F", "Dashboard",     "dashboard"));
        mainItems.Children.Add(MakeNavItem("\uE790", "Visuals",       "visuals"));
        mainItems.Children.Add(MakeNavItem("\uE945", "Desktop FX",    "desktopfx"));
        mainItems.Children.Add(MakeNavItem("\uE91B", "Wallpaper & Colors", "look"));
        mainItems.Children.Add(MakeNavItem("\uE771", "Themes",        "themes"));
        mainItems.Children.Add(MakeNavItem("\uECAA", "App Colors",    "appcolors"));
        mainItems.Children.Add(MakeNavItem("\uE7E8", "Behavior",      "behavior"));
        mainItems.Children.Add(MakeNavItem("\uE7F4", "Desktop Items", "desktop"));
        Grid.SetRow(mainItems, 1);
        grid.Children.Add(mainItems);

        // ── Footer nav items ───────────────────────────────────────────────
        var footer = new StackPanel { Spacing = 2, Margin = new Thickness(8, 4, 8, 12) };
        footer.Children.Add(MakeNavItem("\uE823", "Schedule", "schedule"));
        footer.Children.Add(MakeNavItem("\uE946", "About",    "about"));
        footer.Children.Add(MakeNavItem("\uE713", "Settings", "settings"));
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
        // Left selection indicator (3 × 16 px orange pill).
        var indicator = new Rectangle
        {
            Width           = 3,
            Height          = 16,
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
            FontSize          = 14,
            FontFamily        = new FontFamily("Segoe UI Variable Text"),
            VerticalAlignment = VerticalAlignment.Center,
        };

        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 14 };
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

        // SubtleButtonStyle (App.xaml) supplies the hover/pressed tint and focus visual.
        var btn = new Button
        {
            Style                      = (Style)Application.Current.Resources["SubtleButtonStyle"],
            Content                    = itemGrid,
            Tag                        = tag,
            HorizontalAlignment        = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            Padding                    = new Thickness(4, 9, 12, 9),
            CornerRadius               = new CornerRadius(4),
        };
        ToolTipService.SetToolTip(btn, label);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(btn, label);
        btn.Click += NavItem_Click;
        return btn;
    }

    private void SelectNavItem(StackPanel panel, string tag)
    {
        // Shared brush: ThemeService recolors it, so the selection follows light/dark.
        var selectedBrush = (Brush)Application.Current.Resources["AppNavSelectedBrush"];

        foreach (var child in panel.Children)
        {
            if (child is not Button btn || btn.Tag is not string btnTag) continue;

            bool selected = btnTag == tag;

            btn.Background = selected
                ? selectedBrush
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
        NavigateTo(tag);
    }

    /// <summary>
    /// Selects the sidebar item with this tag and shows its page. Pages call this to
    /// link to each other (for example Visuals → Desktop FX).
    /// </summary>
    public void NavigateTo(string tag)
    {
        _currentTag = tag;
        Logger.Info("Nav", $"Click → {tag}");

        if (_paneMainItems   is not null) SelectNavItem(_paneMainItems,   tag);
        if (_paneFooterItems is not null) SelectNavItem(_paneFooterItems, tag);

        Type? pageType = tag switch
        {
            "dashboard" => typeof(DashboardPage),
            "visuals"   => typeof(VisualsPage),
            "desktopfx" => typeof(DesktopFxPage),
            "look"      => typeof(DesktopLookPage),
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

    // ── Caption buttons follow the app theme ───────────────────────────────

    private void ApplyCaptionButtonTheme(bool isDark)
    {
        try
        {
            var tb = AppWindow.TitleBar;
            tb.ButtonForegroundColor = isDark ? Colors.White : Colors.Black;
            tb.ButtonInactiveForegroundColor = isDark
                ? Windows.UI.Color.FromArgb(0xFF, 0x9D, 0x9D, 0x9D)
                : Windows.UI.Color.FromArgb(0xFF, 0x80, 0x80, 0x80);
            tb.ButtonHoverForegroundColor   = isDark ? Colors.White : Colors.Black;
            tb.ButtonPressedForegroundColor = isDark ? Colors.White : Colors.Black;
            if (!_titleBarAccented)
            {
                byte shade = isDark ? (byte)0xFF : (byte)0x00;
                tb.ButtonHoverBackgroundColor   = Windows.UI.Color.FromArgb(0x20, shade, shade, shade);
                tb.ButtonPressedBackgroundColor = Windows.UI.Color.FromArgb(0x40, shade, shade, shade);
            }
        }
        catch { }
    }

    private bool _titleBarAccented;

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

    /// <summary>Syncs the desktop icon glow color when a theme is applied.</summary>
    public void ApplyDesktopGlowColor(byte r, byte g, byte b)
        => _desktopIconGlow.SetColor(r, g, b);

    // ── Titlebar accent tint ───────────────────────────────────────────────

    /// <summary>
    /// Tints the caption button area to a subtle version of the active glow color.
    /// Called by DashboardPage whenever a preset is applied.
    /// </summary>
    public void SetTitleBarAccent(byte r, byte g, byte b)
    {
        try
        {
            var tb = AppWindow.TitleBar;
            // Titlebar is transparent (content extends into it), so only accent
            // the caption button hover/pressed states with the active glow color.
            _titleBarAccented = true;
            tb.ButtonHoverBackgroundColor   = Windows.UI.Color.FromArgb(0x28, r, g, b);
            tb.ButtonPressedBackgroundColor = Windows.UI.Color.FromArgb(0x50, r, g, b);
        }
        catch { }
    }
}
