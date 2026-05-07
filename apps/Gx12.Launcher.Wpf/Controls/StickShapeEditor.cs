using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using Gx12.Launcher.Wpf.Models;
using Gx12.Launcher.Wpf.Services;

namespace Gx12.Launcher.Wpf.Controls;

public sealed class StickShapeEditor : FrameworkElement
{
    public static readonly DependencyProperty NodesTextProperty = DependencyProperty.Register(
        nameof(NodesText),
        typeof(string),
        typeof(StickShapeEditor),
        new FrameworkPropertyMetadata(
            "[]",
            FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            OnNodesTextChanged));

    public static readonly DependencyProperty ShapingEnabledProperty = DependencyProperty.Register(
        nameof(ShapingEnabled),
        typeof(bool),
        typeof(StickShapeEditor),
        new FrameworkPropertyMetadata(true, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty CurveBrushProperty = DependencyProperty.Register(
        nameof(CurveBrush),
        typeof(Brush),
        typeof(StickShapeEditor),
        new FrameworkPropertyMetadata(Brushes.DeepSkyBlue, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty HintProperty = DependencyProperty.Register(
        nameof(Hint),
        typeof(string),
        typeof(StickShapeEditor),
        new FrameworkPropertyMetadata("Linear", FrameworkPropertyMetadataOptions.AffectsRender));

    private readonly List<StickShapeNode> _nodes = new();
    private int _draggingIndex = -1;
    private int _hoverIndex = -1;
    private bool _isDragging;
    private bool _isInternalUpdate;
    private Point? _lastPointer;

    public StickShapeEditor()
    {
        Focusable = true;
        MinHeight = 110;
        Cursor = Cursors.Cross;
        SnapsToDevicePixels = true;
        ReloadNodes(NodesText);
    }

    public string NodesText
    {
        get => (string)GetValue(NodesTextProperty);
        set => SetValue(NodesTextProperty, value);
    }

    public bool ShapingEnabled
    {
        get => (bool)GetValue(ShapingEnabledProperty);
        set => SetValue(ShapingEnabledProperty, value);
    }

    public Brush CurveBrush
    {
        get => (Brush)GetValue(CurveBrushProperty);
        set => SetValue(CurveBrushProperty, value);
    }

    public string Hint
    {
        get => (string)GetValue(HintProperty);
        set => SetValue(HintProperty, value);
    }

    protected override Size MeasureOverride(Size availableSize)
    {
        var width = double.IsInfinity(availableSize.Width) ? 260 : availableSize.Width;
        return new Size(Math.Max(160, width), Math.Max(MinHeight, 120));
    }

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);

        var bounds = new Rect(0, 0, ActualWidth, ActualHeight);
        if (bounds.Width <= 1 || bounds.Height <= 1)
        {
            return;
        }

        drawingContext.DrawRectangle(new SolidColorBrush(Color.FromRgb(24, 29, 32)), null, bounds);
        var plot = GetPlot();
        DrawGrid(drawingContext, plot);
        DrawCurve(drawingContext, plot);
        DrawNodes(drawingContext, plot);
        DrawNodeReadout(drawingContext, plot);
        DrawHint(drawingContext, plot);
    }

    protected override void OnMouseDown(MouseButtonEventArgs e)
    {
        base.OnMouseDown(e);
        Focus();

        var point = e.GetPosition(this);
        _lastPointer = point;
        if (e.ChangedButton == MouseButton.Right)
        {
            var index = FindNodeAt(point, 12);
            if (index >= 0)
            {
                _nodes.RemoveAt(index);
                _draggingIndex = -1;
                _hoverIndex = -1;
                CommitNodesText();
                InvalidateVisual();
            }

            e.Handled = true;
            return;
        }

        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        var selectedIndex = FindNodeAt(point, 12);
        if (selectedIndex < 0)
        {
            var plot = GetPlot();
            var node = new StickShapeNode(
                ScreenXToNorm(point.X, plot),
                ScreenYToNorm(point.Y, plot),
                StickShapeCurve.DefaultWidth);
            _nodes.Add(node);
            selectedIndex = _nodes.Count - 1;
        }

        _draggingIndex = selectedIndex;
        _hoverIndex = selectedIndex;
        _isDragging = true;
        CaptureMouse();
        InvalidateVisual();
        e.Handled = true;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        var point = e.GetPosition(this);
        _lastPointer = point;
        if (_isDragging && _draggingIndex >= 0 && _draggingIndex < _nodes.Count)
        {
            var plot = GetPlot();
            var current = _nodes[_draggingIndex];
            _nodes[_draggingIndex] = current with
            {
                X = ScreenXToNorm(point.X, plot),
                Y = ScreenYToNorm(point.Y, plot)
            };
            InvalidateVisual();
            e.Handled = true;
            return;
        }

        var hover = FindNodeAt(point, 10);
        if (hover != _hoverIndex)
        {
            _hoverIndex = hover;
            Cursor = hover >= 0 ? Cursors.Hand : Cursors.Cross;
            InvalidateVisual();
        }
    }

    protected override void OnMouseUp(MouseButtonEventArgs e)
    {
        base.OnMouseUp(e);
        if (!_isDragging)
        {
            return;
        }

        _lastPointer = e.GetPosition(this);
        _isDragging = false;
        _draggingIndex = -1;
        ReleaseMouseCapture();
        _hoverIndex = FindNodeAt(e.GetPosition(this), 10);
        CommitNodesText();
        InvalidateVisual();
        e.Handled = true;
    }

    protected override void OnMouseLeave(MouseEventArgs e)
    {
        base.OnMouseLeave(e);
        if (_isDragging)
        {
            return;
        }

        _hoverIndex = -1;
        _lastPointer = null;
        Cursor = Cursors.Cross;
        InvalidateVisual();
    }

    protected override void OnMouseWheel(MouseWheelEventArgs e)
    {
        base.OnMouseWheel(e);
        var point = e.GetPosition(this);
        var index = FindNodeAt(point, 32);
        if (index < 0)
        {
            index = _hoverIndex;
        }

        if (index < 0 && _nodes.Count == 1)
        {
            index = 0;
        }

        if (index < 0 || index >= _nodes.Count)
        {
            return;
        }

        var current = _nodes[index];
        var delta = e.Delta > 0 ? 0.02 : -0.02;
        _nodes[index] = current with { Width = StickShapeCurve.ClampWidth(current.Width + delta) };
        CommitNodesText();
        InvalidateVisual();
        e.Handled = true;
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (_hoverIndex < 0 || _hoverIndex >= _nodes.Count)
        {
            return;
        }

        if (e.Key is Key.Delete or Key.Back)
        {
            _nodes.RemoveAt(_hoverIndex);
            _hoverIndex = -1;
            _draggingIndex = -1;
            CommitNodesText();
            InvalidateVisual();
            e.Handled = true;
            return;
        }

        var dx = 0.0;
        var dy = 0.0;
        switch (e.Key)
        {
            case Key.Left:
                dx = -1.0;
                break;
            case Key.Right:
                dx = 1.0;
                break;
            case Key.Down:
                dy = -1.0;
                break;
            case Key.Up:
                dy = 1.0;
                break;
            default:
                return;
        }

        var step = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift) ? 0.05 : 0.01;
        var current = _nodes[_hoverIndex];
        _nodes[_hoverIndex] = current with
        {
            X = StickShapeCurve.Clamp01(current.X + (dx * step)),
            Y = StickShapeCurve.Clamp01(current.Y + (dy * step))
        };
        CommitNodesText();
        InvalidateVisual();
        e.Handled = true;
    }

    protected override void OnLostMouseCapture(MouseEventArgs e)
    {
        base.OnLostMouseCapture(e);
        if (!_isDragging)
        {
            return;
        }

        _isDragging = false;
        _draggingIndex = -1;
        CommitNodesText();
        InvalidateVisual();
    }

    private static void OnNodesTextChanged(DependencyObject dependencyObject, DependencyPropertyChangedEventArgs eventArgs)
    {
        var editor = (StickShapeEditor)dependencyObject;
        if (editor._isInternalUpdate)
        {
            return;
        }

        editor.ReloadNodes(eventArgs.NewValue as string);
        editor.InvalidateVisual();
    }

    private void ReloadNodes(string? text)
    {
        _nodes.Clear();
        _nodes.AddRange(StickShapeCurve.ParseNodes(text));
        _draggingIndex = -1;
        _hoverIndex = -1;
    }

    private void CommitNodesText()
    {
        _isInternalUpdate = true;
        try
        {
            NodesText = StickShapeCurve.FormatNodes(_nodes);
        }
        finally
        {
            _isInternalUpdate = false;
        }
    }

    private Rect GetPlot()
    {
        const double pad = 12.0;
        return new Rect(
            pad,
            pad,
            Math.Max(1.0, ActualWidth - (pad * 2.0)),
            Math.Max(1.0, ActualHeight - (pad * 2.0)));
    }

    private void DrawGrid(DrawingContext drawingContext, Rect plot)
    {
        var gridPen = new Pen(new SolidColorBrush(Color.FromRgb(46, 55, 60)), 1);
        var axisPen = new Pen(new SolidColorBrush(Color.FromRgb(90, 105, 111)), 1);
        var baselinePen = new Pen(new SolidColorBrush(Color.FromRgb(110, 126, 134)), 1)
        {
            DashStyle = DashStyles.Dash
        };

        for (var index = 1; index < 4; index++)
        {
            var x = plot.Left + (plot.Width * index / 4.0);
            var y = plot.Top + (plot.Height * index / 4.0);
            drawingContext.DrawLine(gridPen, new Point(x, plot.Top), new Point(x, plot.Bottom));
            drawingContext.DrawLine(gridPen, new Point(plot.Left, y), new Point(plot.Right, y));
        }

        drawingContext.DrawLine(axisPen, new Point(plot.Left, plot.Bottom), new Point(plot.Right, plot.Bottom));
        drawingContext.DrawLine(axisPen, new Point(plot.Left, plot.Top), new Point(plot.Left, plot.Bottom));
        drawingContext.DrawLine(baselinePen, new Point(plot.Left, plot.Bottom), new Point(plot.Right, plot.Top));
    }

    private void DrawCurve(DrawingContext drawingContext, Rect plot)
    {
        var brush = ShapingEnabled ? CurveBrush : new SolidColorBrush(Color.FromRgb(93, 105, 111));
        var pen = new Pen(brush, 2);
        var geometry = new StreamGeometry();
        using (var context = geometry.Open())
        {
            var samples = Math.Max(160, Math.Min(1600, (int)(plot.Width * 2)));
            for (var index = 0; index <= samples; index++)
            {
                var t = (double)index / samples;
                var y = StickShapeCurve.Evaluate(t, _nodes);
                var point = new Point(plot.Left + (t * plot.Width), plot.Bottom - (y * plot.Height));
                if (index == 0)
                {
                    context.BeginFigure(point, false, false);
                }
                else
                {
                    context.LineTo(point, true, false);
                }
            }
        }

        geometry.Freeze();
        drawingContext.DrawGeometry(null, pen, geometry);
    }

    private void DrawNodes(DrawingContext drawingContext, Rect plot)
    {
        var curveBrush = ShapingEnabled ? CurveBrush : new SolidColorBrush(Color.FromRgb(93, 105, 111));
        var widthBrush = CreateAlphaBrush(curveBrush, 72);
        var fillBrush = new SolidColorBrush(Color.FromRgb(24, 29, 32));
        var pen = new Pen(curveBrush, 2);

        for (var index = 0; index < _nodes.Count; index++)
        {
            var node = _nodes[index];
            var center = NodeToScreen(node, plot);
            var widthPx = StickShapeCurve.ClampWidth(node.Width) * plot.Width;
            drawingContext.DrawRectangle(
                widthBrush,
                null,
                new Rect(center.X - widthPx, plot.Bottom - 7, widthPx * 2.0, 4));

            var radius = index == _hoverIndex || index == _draggingIndex ? 7.0 : 5.0;
            drawingContext.DrawEllipse(fillBrush, pen, center, radius, radius);
        }
    }

    private void DrawHint(DrawingContext drawingContext, Rect plot)
    {
        if (_nodes.Count != 0 || string.IsNullOrWhiteSpace(Hint))
        {
            return;
        }

        var text = new FormattedText(
            Hint,
            System.Globalization.CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            new Typeface("Segoe UI"),
            12,
            new SolidColorBrush(Color.FromRgb(128, 142, 148)),
            VisualTreeHelper.GetDpi(this).PixelsPerDip);

        drawingContext.DrawText(text, new Point(plot.Left + 5, plot.Top + 4));
    }

    private void DrawNodeReadout(DrawingContext drawingContext, Rect plot)
    {
        var index = _draggingIndex >= 0 ? _draggingIndex : _hoverIndex;
        if (index < 0 || index >= _nodes.Count)
        {
            return;
        }

        var node = _nodes[index];
        var text = new FormattedText(
            $"x {node.X:0.00}  y {node.Y:0.00}  w {node.Width:0.00}",
            System.Globalization.CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            new Typeface("Segoe UI"),
            11,
            new SolidColorBrush(Color.FromRgb(229, 233, 234)),
            VisualTreeHelper.GetDpi(this).PixelsPerDip);

        var anchor = _lastPointer ?? NodeToScreen(node, plot);
        var x = Math.Min(plot.Right - text.Width - 12, Math.Max(plot.Left + 6, anchor.X + 12));
        var y = Math.Min(plot.Bottom - text.Height - 10, Math.Max(plot.Top + 6, anchor.Y - text.Height - 12));
        var background = new Rect(x - 6, y - 4, text.Width + 12, text.Height + 8);
        drawingContext.DrawRoundedRectangle(
            new SolidColorBrush(Color.FromArgb(236, 18, 22, 24)),
            new Pen(new SolidColorBrush(Color.FromRgb(76, 88, 94)), 1),
            background,
            4,
            4);
        drawingContext.DrawText(text, new Point(x, y));
    }

    private int FindNodeAt(Point point, double radius)
    {
        var plot = GetPlot();
        var radiusSquared = radius * radius;
        for (var index = _nodes.Count - 1; index >= 0; index--)
        {
            var center = NodeToScreen(_nodes[index], plot);
            var dx = center.X - point.X;
            var dy = center.Y - point.Y;
            if ((dx * dx) + (dy * dy) <= radiusSquared)
            {
                return index;
            }
        }

        return -1;
    }

    private static Point NodeToScreen(StickShapeNode node, Rect plot)
    {
        return new Point(
            plot.Left + (StickShapeCurve.Clamp01(node.X) * plot.Width),
            plot.Bottom - (StickShapeCurve.Clamp01(node.Y) * plot.Height));
    }

    private static double ScreenXToNorm(double x, Rect plot)
    {
        return StickShapeCurve.Clamp01((x - plot.Left) / Math.Max(1.0, plot.Width));
    }

    private static double ScreenYToNorm(double y, Rect plot)
    {
        return StickShapeCurve.Clamp01((plot.Bottom - y) / Math.Max(1.0, plot.Height));
    }

    private static Brush CreateAlphaBrush(Brush source, byte alpha)
    {
        if (source is SolidColorBrush solid)
        {
            return new SolidColorBrush(Color.FromArgb(alpha, solid.Color.R, solid.Color.G, solid.Color.B));
        }

        return new SolidColorBrush(Color.FromArgb(alpha, 79, 183, 161));
    }
}
