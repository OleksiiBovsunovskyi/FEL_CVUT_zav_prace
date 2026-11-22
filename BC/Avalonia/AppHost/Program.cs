using Avalonia;
using AppView;

namespace AppHost;

internal static class Program
{
    /// <param name="args"> first argument: folder holding pgr_embed.dll, Shaders and Assets.</param>
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Length > 0) RendererView.DataRoot = Path.GetFullPath(args[0]);
        else throw new ApplicationException("Please provide engine .dll");
        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .With(new Win32PlatformOptions
            {
                //Render with Vulkan to match engine backend
                RenderingMode = [Win32RenderingMode.Vulkan]
            });
}
