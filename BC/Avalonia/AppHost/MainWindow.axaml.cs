using AppView;
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
        Renderer.FrameMetricsReceived += AppendStatsColumn;
        Renderer.RendererStateUpdated += UpdateRendererStatus;
        TestGraph.ValueMax = 16; //16ms frame budget, probably will expose setting to control this.

        ShowVertPosBits.IsCheckedChanged += (_, _) => UpdateDebugFlags();
        ShowMeshlets.IsCheckedChanged += (_, _) => UpdateDebugFlags();
        ShowMeshletsBounds.IsCheckedChanged += (_, _) => UpdateDebugFlags();
    }

    private void UpdateDebugFlags()
    {
        var flags = EngineDebugFlags.None;
        if (ShowVertPosBits.IsChecked == true) flags |= EngineDebugFlags.ShowVertexPositionSize;
        if (ShowMeshlets.IsChecked == true) flags |= EngineDebugFlags.ShowMeshlets;
        if (ShowMeshletsBounds.IsChecked == true) flags |= EngineDebugFlags.ShowMeshletSpheres;
        Renderer.DebugFlags = flags;
    }
    
    private void AppendStatsColumn(IReadOnlyList<AppView.FrameMetric> metric)
    {
        var segments = new List<ColumnSegment> { };
        double total = 0;
        foreach (var frameMetric in metric)
        {
            switch (frameMetric.Name)
            {
                case "scatter_instances":
                    segments.Add(new(Colors.Red, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "build_draw_commands":
                    segments.Add(new(Colors.Orange, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "mesh draw":
                    segments.Add(new(Colors.ForestGreen, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "draw callback":
                    segments.Add(new(Colors.DodgerBlue, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "build depth pyramid end":
                    segments.Add(new(Colors.MediumPurple, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "Deferred renderer: Visibility pass":
                    segments.Add(new(Colors.Gold, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "Deferred renderer: Draw pass":
                    segments.Add(new(Colors.DeepSkyBlue, frameMetric.Value));
                    total += frameMetric.Value;
                    break;
                case "instances":           //Probably will go into separate graph or just as numbers 
                case "instances_drawn":     //Probably will go into separate graph or just as numbers       
                case "instances_culled":    //Probably will go into separate graph or just as numbers        
                case "meshlets_drawn":      //Probably will go into separate graph or just as numbers      
                case "meshlets_culled":     //Probably will go into separate graph or just as numbers       
                case "triangles_submitted": //Probably will go into separate graph or just as numbers           
                case "triangles_drawn":     //Probably will go into separate graph or just as numbers       
                    break;
            }
        }
        
        TestGraph.AppendColumn(segments, total);
    }

    private void UpdateInputState(bool captured) =>
        InputState.Text = $"Input state (TAB): {(captured ? "Captured" : "Released")}";

    private void UpdateRendererStatus(string state) =>
        RendererStatus.Text = $"Renderer: {state}";
}
