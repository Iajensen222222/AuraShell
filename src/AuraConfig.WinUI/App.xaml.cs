using Microsoft.UI.Xaml;

namespace AuraConfig;

public partial class App : Application
{
    private Window? _window;
    public Window? MainWindow => _window;

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
        _window = new MainWindow();
        _window.Activate();
    }
}
