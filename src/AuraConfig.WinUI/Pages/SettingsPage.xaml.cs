using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace AuraConfig.Pages;

public sealed partial class SettingsPage : Page
{
    private const string KeyTheme         = "AppTheme";          // "Dark" | "Light" | "System"
    private const string KeyNotifications = "ShowNotifications"; // "true" | "false"

    public SettingsPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // Restore saved preferences without triggering handlers.
        ThemeDark.Checked            -= ThemeRadio_Checked;
        ThemeLight.Checked           -= ThemeRadio_Checked;
        ThemeSystem.Checked          -= ThemeRadio_Checked;
        NotificationsToggle.Toggled  -= NotificationsToggle_Toggled;

        string theme = AppSettings.Get(KeyTheme) ?? "Dark";
        (theme switch
        {
            "Light"  => ThemeLight,
            "System" => ThemeSystem,
            _        => ThemeDark
        }).IsChecked = true;
        ApplyTheme(theme);

        NotificationsToggle.IsOn = (AppSettings.Get(KeyNotifications) ?? "true") == "true";

        ThemeDark.Checked            += ThemeRadio_Checked;
        ThemeLight.Checked           += ThemeRadio_Checked;
        ThemeSystem.Checked          += ThemeRadio_Checked;
        NotificationsToggle.Toggled  += NotificationsToggle_Toggled;
    }

    // ── Theme ──────────────────────────────────────────────────────────────

    private void ThemeRadio_Checked(object sender, RoutedEventArgs e)
    {
        if (sender is not RadioButton rb) return;

        string name = rb.Content switch
        {
            "Light"              => "Light",
            "Use system setting" => "System",
            _                   => "Dark"
        };

        AppSettings.Set(KeyTheme, name);
        ApplyTheme(name);
    }

    private void ApplyTheme(string name)
    {
        var theme = name switch
        {
            "Light"  => ElementTheme.Light,
            "System" => ElementTheme.Default,
            _        => ElementTheme.Dark
        };

        if (XamlRoot?.Content is FrameworkElement root)
            root.RequestedTheme = theme;
    }

    // ── Notifications ──────────────────────────────────────────────────────

    private void NotificationsToggle_Toggled(object sender, RoutedEventArgs e)
    {
        AppSettings.Set(KeyNotifications, NotificationsToggle.IsOn ? "true" : "false");
    }

    // ── Reset ──────────────────────────────────────────────────────────────

    private async void ResetDefaults_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new ContentDialog
        {
            Title             = "Reset to defaults?",
            Content           = "This will reset all AuraShell settings to factory defaults. Are you sure?",
            PrimaryButtonText = "Reset",
            CloseButtonText   = "Cancel",
            XamlRoot          = XamlRoot
        };

        var result = await dialog.ShowAsync();
        if (result != ContentDialogResult.Primary) return;

        AppSettings.Clear();

        // Reset UI without firing handlers
        ThemeDark.Checked            -= ThemeRadio_Checked;
        ThemeLight.Checked           -= ThemeRadio_Checked;
        ThemeSystem.Checked          -= ThemeRadio_Checked;
        NotificationsToggle.Toggled  -= NotificationsToggle_Toggled;

        ThemeDark.IsChecked      = true;
        NotificationsToggle.IsOn = true;

        ThemeDark.Checked            += ThemeRadio_Checked;
        ThemeLight.Checked           += ThemeRadio_Checked;
        ThemeSystem.Checked          += ThemeRadio_Checked;
        NotificationsToggle.Toggled  += NotificationsToggle_Toggled;

        ApplyTheme("Dark");
    }
}
