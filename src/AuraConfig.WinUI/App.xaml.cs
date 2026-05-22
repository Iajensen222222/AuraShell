using Microsoft.UI.Xaml;

namespace AuraConfig;

public partial class App : Application
{
    private MainWindow? _mainWindow;
    public MainWindow? MainWindow => _mainWindow;
    // Legacy alias for code that only needs Window APIs
    public Window? Window => _mainWindow;

    public App()
    {
        this.UnhandledException += (s, e) =>
        {
            var logPath = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "AuraShell", "winui_crash.log");
            try
            {
                System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(logPath)!);
                System.IO.File.AppendAllText(logPath,
                    $"[{DateTime.Now:HH:mm:ss.fff}] UnhandledException: {e.Exception}\n");
            }
            catch { }
            e.Handled = false;
        };

        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _mainWindow = new MainWindow();
        _mainWindow.Activate();
    }
}
