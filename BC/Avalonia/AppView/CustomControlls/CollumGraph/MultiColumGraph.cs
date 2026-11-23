using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;

namespace AppView.CustomControlls.CollumGraph;

/// <summary>
/// Renders appended MultiColumn controls side by side with the newest on the right,
/// evicting the oldest columns when the total width exceeds the control bounds.
/// </summary>
public class MultiColumnGraph : Control
{
    private readonly Queue<MultiColumn> _columns = new();

    public static readonly StyledProperty<double> ColumnWidthProperty =
        AvaloniaProperty.Register<MultiColumnGraph, double>(
            nameof(ColumnWidth),
            defaultValue: 10.0,
            coerce: CoerceColumnWidth);

    public static readonly StyledProperty<double> ValueMaxProperty =
        AvaloniaProperty.Register<MultiColumnGraph, double>(
            nameof(ValueMax),
            defaultValue: 100.0,
            coerce: CoerceValueMax);

    public double ColumnWidth
    {
        get => GetValue(ColumnWidthProperty);
        set => SetValue(ColumnWidthProperty, value);
    }

    public double ValueMax
    {
        get => GetValue(ValueMaxProperty);
        set => SetValue(ValueMaxProperty, value);
    }

    static MultiColumnGraph()
    {
        AffectsMeasure<MultiColumnGraph>(ColumnWidthProperty);
        AffectsArrange<MultiColumnGraph>(ColumnWidthProperty, ValueMaxProperty);
    }

    public MultiColumnGraph()
    {
        ClipToBounds = true;
    }

    public void AppendColumn(IList<ColumnSegment> segments, double value)
    {
        ArgumentNullException.ThrowIfNull(segments);

        var column = new MultiColumn { Segments = segments, Value = value };
        _columns.Enqueue(column);
        LogicalChildren.Add(column);
        VisualChildren.Add(column);

        TrimOverflow(Bounds.Width);
        InvalidateMeasure();
        InvalidateArrange();
    }

    public void Clear()
    {
        if (_columns.Count == 0)
            return;

        while (_columns.Count > 0)
        {
            var column = _columns.Dequeue();
            VisualChildren.Remove(column);
            LogicalChildren.Remove(column);
        }

        InvalidateMeasure();
        InvalidateArrange();
    }

    protected override void OnSizeChanged(SizeChangedEventArgs e)
    {
        base.OnSizeChanged(e);

        if (TrimOverflow(e.NewSize.Width))
            InvalidateArrange();
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == ColumnWidthProperty && TrimOverflow(Bounds.Width))
            InvalidateArrange();
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        double columnWidth = ColumnWidth;
        double childHeight = double.IsInfinity(availableSize.Height) ? 0 : availableSize.Height;
        var childSize = new Size(columnWidth, childHeight);

        foreach (var column in _columns)
            column.Measure(childSize);

        double desiredWidth = _columns.Count * columnWidth;
        if (!double.IsInfinity(availableSize.Width))
            desiredWidth = Math.Min(desiredWidth, availableSize.Width);

        return new Size(desiredWidth, childHeight);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        double columnWidth = ColumnWidth;
        double valueMax = ValueMax;
        double x = 0;

        foreach (var column in _columns)
        {
            double scale = valueMax > 0 && !double.IsNaN(column.Value)
                ? Math.Clamp(column.Value / valueMax, 0.0, 1.0)
                : 0.0;
            double columnHeight = finalSize.Height * scale;
            double y = finalSize.Height - columnHeight;

            column.Arrange(new Rect(x, y, columnWidth, columnHeight));
            x += columnWidth;
        }

        return finalSize;
    }

    private bool TrimOverflow(double availableWidth)
    {
        if (availableWidth <= 0)
            return false;

        int maxColumns = (int)Math.Floor(availableWidth / ColumnWidth);
        bool removed = false;

        while (_columns.Count > maxColumns)
        {
            var oldest = _columns.Dequeue();
            VisualChildren.Remove(oldest);
            LogicalChildren.Remove(oldest);
            removed = true;
        }

        return removed;
    }

    private static double CoerceColumnWidth(AvaloniaObject sender, double value) =>
        double.IsFinite(value) && value > 0 ? value : 1.0;

    private static double CoerceValueMax(AvaloniaObject sender, double value) =>
        double.IsFinite(value) && value >= 0 ? value : 0.0;
}
