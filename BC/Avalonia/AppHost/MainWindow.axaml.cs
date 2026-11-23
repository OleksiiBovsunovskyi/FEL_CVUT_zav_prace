using AppView.CustomControlls.CollumGraph;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;

namespace AppHost;

public partial class MainWindow : Window
{
    private readonly DispatcherTimer _graphTimer;

    public MainWindow()
    {
        InitializeComponent();
        Renderer.CursorCapturedChanged += UpdateInputState;
        UpdateInputState(Renderer.IsCursorCaptured);

        _graphTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        _graphTimer.Tick += (_, _) => AppendRandomColumn();
        _graphTimer.Start();
        Closed += (_, _) => _graphTimer.Stop();
    }

    private void AppendRandomColumn()
    {
        var rng = Random.Shared;
        var segments = new List<ColumnSegment>
        {
            new(Colors.ForestGreen, rng.Next(5, 100)),
            new(Color.Parse("#FF5733"), rng.Next(5, 100)),
            new(Color.FromRgb(40, 120, 220), rng.Next(5, 100))
        };
        double value = rng.Next(0, 125);
        TestGraph.AppendColumn(segments, value);
    }

    private void UpdateInputState(bool captured) =>
        InputState.Text = $"Input state (TAB): {(captured ? "Captured" : "Released")}";

    private void UpdateRendererStatus(bool startingUp) =>
        RendererStatus.IsVisible = startingUp;
}
