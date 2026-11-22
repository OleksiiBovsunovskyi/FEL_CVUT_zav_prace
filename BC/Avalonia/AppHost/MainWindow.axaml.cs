using Avalonia.Controls;

namespace AppHost;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Renderer.CursorCapturedChanged += UpdateInputState;
        UpdateInputState(Renderer.IsCursorCaptured);
        
    }

    private void UpdateInputState(bool captured) =>
        InputState.Text = $"Input state (TAB): {(captured ? "Captured" : "Released")}";

    private void UpdateRendererStatus(bool startingUp) =>
        RendererStatus.IsVisible = startingUp;
}
