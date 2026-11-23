using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace AppView.CustomControlls.CollumGraph;
public readonly record struct ColumnSegment(Color Color, double Percent);

/// <summary>
/// Vertical column broken down into colored vertical stretches based on their Percent (normalized).
/// </summary>
public class MultiColumn : Control
{
    private static readonly Dictionary<Color, IBrush> BrushCache = new();

    public static readonly StyledProperty<IList<ColumnSegment>?> SegmentsProperty =
        AvaloniaProperty.Register<MultiColumn, IList<ColumnSegment>?>(
            nameof(Segments),
            coerce: CoerceSegments);

    public static readonly StyledProperty<double> ValueProperty =
        AvaloniaProperty.Register<MultiColumn, double>(
            nameof(Value),
            defaultValue: 0.0);

    public IList<ColumnSegment>? Segments
    {
        get => GetValue(SegmentsProperty);
        set => SetValue(SegmentsProperty, value);
    }

    public double Value
    {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    static MultiColumn()
    {
        // Automatically triggers Render() whenever the Segments property is reassigned
        AffectsRender<MultiColumn>(SegmentsProperty);
    }

    private static IList<ColumnSegment>? CoerceSegments(AvaloniaObject sender, IList<ColumnSegment>? value)
    {
        if (value is null)
            return null;

        double segmentsPSumm = 0;
        foreach (var columnSegment in value)
            segmentsPSumm += columnSegment.Percent;

        var newSegments = new List<ColumnSegment>(value.Count);
        foreach (var columnSegment in value)
        {
            double normalizedPercent = segmentsPSumm > 0
                ? (columnSegment.Percent / segmentsPSumm) * 100.0
                : 0;

            newSegments.Add(new ColumnSegment(columnSegment.Color, normalizedPercent));
        }

        return newSegments;
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        if (Segments is null || Segments.Count == 0 || Bounds.Width <= 0 || Bounds.Height <= 0)
            return;

        // Draws from bottom to top in list order (index 0 at the bottom)
        double currentY = Bounds.Height;

        foreach (var segment in Segments)
        {
            if (segment.Percent <= 0) continue;

            double segmentHeight = (segment.Percent / 100.0) * Bounds.Height;
            double topY = Math.Max(0, currentY - segmentHeight);
            double drawHeight = currentY - topY;

            if (drawHeight > 0)
            {
                context.FillRectangle(GetBrush(segment.Color), new Rect(0, topY, Bounds.Width, drawHeight));
            }

            currentY = topY;
            if (currentY <= 0) break;
        }
    }

    private static IBrush GetBrush(Color color)
    {
        if (!BrushCache.TryGetValue(color, out var brush))
        {
            brush = new SolidColorBrush(color);
            BrushCache[color] = brush;
        }
        return brush;
    }
}