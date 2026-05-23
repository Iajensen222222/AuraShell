using AuraConfig.Services;
using Microsoft.UI.Xaml;

namespace AuraConfig;

public partial class App : Application
{
    private MainWindow? _mainWindow;
    public MainWindow? MainWindow => _mainWindow;
    // Legacy alias for code that only needs Window APIs
    public Window? Window => _mainWindow;

    // Single-instance guard — keeps multiple AuraConfig processes from racing
    // each other to set DwmSetWindowAttribute, which produced the
    // 'rainbow' flicker the user reported.
    private static System.Threading.Mutex? _singleInstance;

    public App()
    {
        Logger.Info("App", "Process starting");

        bool createdNew;
        _singleInstance = new System.Threading.Mutex(true, "AuraConfig_SingleInstance", out createdNew);
        if (!createdNew)
        {
            Logger.Warn("App", "Another AuraConfig instance is already running — exiting");
            System.Environment.Exit(0);
        }

        // Existing crash dumper — keeps the original winui_crash.log intact for
        // automation that already greps it.
        this.UnhandledException += (s, e) =>
        {
            try
            {
                Logger.Error("App", "UnhandledException", e.Exception);

                var crashPath = System.IO.Path.Combine(
                    System.Environment.GetFolderPath(System.Environment.SpecialFolder.LocalApplicationData),
                    "AuraShell", "winui_crash.log");
                System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(crashPath)!);
                System.IO.File.AppendAllText(crashPath,
                    $"[{System.DateTime.Now:HH:mm:ss.fff}] UnhandledException: {e.Exception}\n");
            }
            catch { }
            e.Handled = false;
        };

        InitializeComponent();
        Logger.Debug("App", "InitializeComponent done");
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Logger.Info("App", "OnLaunched");
        _mainWindow = new MainWindow();
        _mainWindow.Activate();
        Logger.Info("App", "MainWindow activated");
    }
}
