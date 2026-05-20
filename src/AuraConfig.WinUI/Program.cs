using Microsoft.Windows.ApplicationModel.DynamicDependency;

namespace AuraConfig;

public static class Program
{
    [System.STAThread]
    static void Main(string[] args)
    {
        // Bootstrap the Windows App Runtime so ms-appx: URIs (WinUI themes, XBF, etc.)
        // resolve correctly. Required for unpackaged apps since DISABLE_XAML_GENERATED_MAIN
        // suppresses the auto-generated Main that would normally call this.
        Bootstrap.Initialize(0x00020000); // WinAppSDK 2.0.x

        WinRT.ComWrappersSupport.InitializeComWrappers();

        Microsoft.UI.Xaml.Application.Start((p) =>
        {
            var ctx = new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(
                Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread());
            System.Threading.SynchronizationContext.SetSynchronizationContext(ctx);
            new App();
        });
    }
}
