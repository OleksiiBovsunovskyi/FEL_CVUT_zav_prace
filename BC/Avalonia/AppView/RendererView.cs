using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Rendering.Composition;
using Avalonia.Threading;

namespace AppView;

/// <summary>
/// Composites GPU-shared frames from the native renderer and forwards keys and pointer motion while focused.
/// Clicking captures the cursor; Tab, focus loss, or window deactivation releases it.
/// Other controls can be placed over it.
/// </summary>
public sealed class RendererView : Control
{
    private const int VkImageLayoutShaderReadOnlyOptimal = 5;

    private sealed class ImportedSlot
    {
        public required ICompositionImportedGpuImage Image { get; init; }
        public required ICompositionImportedGpuSemaphore RenderDone { get; init; }
        public required ICompositionImportedGpuSemaphore AvaloniaDone { get; init; }
        public Task? PendingUpdate { get; set; }

        public Task DisposeAsync() => Task.WhenAll(
            Image.DisposeAsync().AsTask(),
            RenderDone.DisposeAsync().AsTask(),
            AvaloniaDone.DisposeAsync().AsTask());
    }

    private readonly object _slotsLock = new();
    private ImportedSlot[] _slots = [];
    private ICompositionGpuInterop? _interop;
    private CompositionDrawingSurface? _surface;
    private CompositionSurfaceVisual? _surfaceVisual;
    private NativeRenderer? _renderer;
    private PixelSize _requested;
    private string? _error;
    private IReadOnlyList<FrameMetric> _latestMetrics = [];
    private volatile bool _attached;
    private volatile bool _detaching;
    private bool _cursorCaptured;
    private PixelPoint _captureCenter;
    private Window? _window;
    private readonly HashSet<byte> _keysDown = new();

    public RendererView()
    {
        Focusable = true;
        ClipToBounds = true;
    }

    /// <summary>Folder holding app.dll, Shaders and Assets. Set before the first view is shown.</summary>
    public static string? DataRoot { get; set; }

    /// <summary>Text of the failure that stopped the renderer, null while it runs.</summary>
    public string? Error => _error;

    /// <summary>Whether the view currently captures the OS cursor for relative pointer deltas.</summary>
    public bool IsCursorCaptured => _cursorCaptured;

    /// <summary>Raised on the UI thread whenever <see cref="IsCursorCaptured"/> changes.</summary>
    public event Action<bool>? CursorCapturedChanged;

    /// <summary>Measurements of the most recently completed frame.</summary>
    public IReadOnlyList<FrameMetric> LatestMetrics => _latestMetrics;

    /// <summary>Raised on the UI thread when a completed frame's metrics arrive from the native renderer.</summary>
    public event Action<IReadOnlyList<FrameMetric>>? FrameMetricsReceived;

    internal static string ResolveDataRoot() =>
        DataRoot ?? Environment.GetEnvironmentVariable("APP_DATA_ROOT") ?? AppContext.BaseDirectory;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _detaching = false;
        _attached = true;
        _window = TopLevel.GetTopLevel(this) as Window;
        if (_window is not null) _window.Deactivated += OnWindowDeactivated;
        _ = InitializeInteropAsync();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        _detaching = true;
        _attached = false;
        ReleaseCursor();
        if (_window is not null)
        {
            _window.Deactivated -= OnWindowDeactivated;
            _window = null;
        }
        DisposeSlotsOnUiThread();
        _renderer?.Dispose();
        _renderer = null;
        ElementComposition.SetElementChildVisual(this, null);
        _surfaceVisual = null;
        _surface?.Dispose();
        _surface = null;
        _interop = null;
        base.OnDetachedFromVisualTree(e);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        Size arranged = base.ArrangeOverride(finalSize);
        if (_surfaceVisual is not null)
            _surfaceVisual.Size = new Vector(arranged.Width, arranged.Height);

        PixelSize pixels = PixelSizeFor(arranged);
        if (_renderer is not null && pixels.Width > 0 && pixels.Height > 0 && pixels != _requested)
        {
            _renderer.Resize((uint)pixels.Width, (uint)pixels.Height);
            _requested = pixels;
        }
        return arranged;
    }

    private async Task InitializeInteropAsync()
    {
        try
        {
            Compositor? compositor = ElementComposition.GetElementVisual(this)?.Compositor;
            if (compositor is null)
                throw new InvalidOperationException("Avalonia Compositor is unavailable.");

            ICompositionGpuInterop? interop = await compositor.TryGetCompositionGpuInterop();
            if (interop is null)
                throw new InvalidOperationException("Avalonia GPU interop is unavailable (Vulkan backend required).");

            if (!interop.SupportedImageHandleTypes.Contains(KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle) ||
                !interop.SupportedSemaphoreTypes.Contains(KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle))
            {
                throw new InvalidOperationException(
                    $"Avalonia backend does not support VulkanOpaqueNtHandle. Images: [{string.Join(", ", interop.SupportedImageHandleTypes)}], Semaphores: [{string.Join(", ", interop.SupportedSemaphoreTypes)}].");
            }

            if (!_attached) return;

            _interop = interop;
            _surface = compositor.CreateDrawingSurface();
            _surfaceVisual = compositor.CreateSurfaceVisual();
            _surfaceVisual.Surface = _surface;
            _surfaceVisual.Size = new Vector(Bounds.Width, Bounds.Height);
            ElementComposition.SetElementChildVisual(this, _surfaceVisual);

            PixelSize initial = PixelSizeFor(Bounds.Size);
            if (initial.Width <= 0 || initial.Height <= 0)
                initial = new PixelSize(1280, 720);
            _requested = initial;

            _renderer = new NativeRenderer(
                ResolveDataRoot(),
                (uint)initial.Width,
                (uint)initial.Height,
                interop.DeviceUuid,
                OnTargetsCreated,
                OnTargetsDestroyed,
                OnPresent,
                OnNativeError,
                OnFrameMetrics);
        }
        catch (Exception ex)
        {
            _error = ex.Message;
            await Console.Error.WriteLineAsync($"RendererView init failed: {ex}");
        }
    }

    private void OnTargetsCreated(ExternalSlotHandles[] handles, int width, int height)
    {
        if (_detaching) return;
        Dispatcher.UIThread.Invoke(() => ImportSlotsOnUiThread(handles, width, height));
    }

    private void ImportSlotsOnUiThread(ExternalSlotHandles[] handles, int width, int height)
    {
        ICompositionGpuInterop? interop = _interop;
        if (interop is null || _detaching) return;

        var imported = new ImportedSlot[handles.Length];
        for (int i = 0; i < handles.Length; i++)
        {
            ExternalSlotHandles slot = handles[i];
            var imageProps = new PlatformGraphicsExternalImageProperties
            {
                Width = width,
                Height = height,
                Format = PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm,
                MemorySize = slot.MemorySize,
                MemoryOffset = slot.MemoryOffset,
                TopLeftOrigin = true,
                VulkanProperties = new PlatformGraphicsExternalImageVulkanProperties
                {
                    Layout = VkImageLayoutShaderReadOnlyOptimal
                }
            };

            ICompositionImportedGpuImage image = interop.ImportImage(
                new PlatformHandle(slot.MemoryHandle, KnownPlatformGraphicsExternalImageHandleTypes.VulkanOpaqueNtHandle),
                imageProps);
            ICompositionImportedGpuSemaphore renderDone = interop.ImportSemaphore(
                new PlatformHandle(slot.RenderDoneSemaphoreHandle, KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle));
            ICompositionImportedGpuSemaphore avaloniaDone = interop.ImportSemaphore(
                new PlatformHandle(slot.AvaloniaDoneSemaphoreHandle, KnownPlatformGraphicsExternalSemaphoreHandleTypes.VulkanOpaqueNtHandle));

            imported[i] = new ImportedSlot
            {
                Image = image,
                RenderDone = renderDone,
                AvaloniaDone = avaloniaDone
            };
        }

        lock (_slotsLock)
            _slots = imported;
    }

    private void OnTargetsDestroyed()
    {
        if (_detaching) return;
        Dispatcher.UIThread.Invoke(DisposeSlotsOnUiThread);
    }

    private void DisposeSlotsOnUiThread()
    {
        ImportedSlot[] oldSlots;
        Task[] pending;
        lock (_slotsLock)
        {
            oldSlots = _slots;
            _slots = [];
            pending = oldSlots.Select(s => s.PendingUpdate).Where(t => t is not null).Cast<Task>().ToArray();
        }

        if (pending.Length > 0)
        {
            try { Task.WhenAll(pending).Wait(TimeSpan.FromSeconds(2)); }
            catch { }
        }

        if (oldSlots.Length > 0)
        {
            try
            {
                Task.WhenAll(oldSlots.Select(s => s.DisposeAsync())).Wait(TimeSpan.FromSeconds(2));
            }
            catch { }
        }
    }

    private void OnPresent(uint slotIndex)
    {
        if (_detaching)
        {
            _renderer?.ReleaseSlot(slotIndex);
            return;
        }

        Dispatcher.UIThread.Post(() => PresentSlotOnUiThread(slotIndex), DispatcherPriority.Render);
    }

    private void PresentSlotOnUiThread(uint slotIndex)
    {
        CompositionDrawingSurface? surface = _surface;
        ImportedSlot? slot;
        lock (_slotsLock)
        {
            if (_detaching || surface is null || slotIndex >= _slots.Length)
            {
                _renderer?.ReleaseSlot(slotIndex);
                return;
            }
            slot = _slots[slotIndex];
        }

        Task update = surface.UpdateWithSemaphoresAsync(slot.Image, slot.RenderDone, slot.AvaloniaDone);
        lock (_slotsLock)
            slot.PendingUpdate = update;

        _ = update.ContinueWith(
            t =>
            {
                if (t.IsFaulted)
                    Console.Error.WriteLine($"UpdateWithSemaphoresAsync faulted: {t.Exception}");
                _renderer?.ReleaseSlot(slotIndex);
            },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    public override void Render(DrawingContext context)
    {
        context.FillRectangle(Brushes.Transparent, new Rect(Bounds.Size));
    }

    private void OnNativeError(string message)
    {
        _error = message;
        Console.Error.WriteLine($"Native renderer error: {message}");
        Dispatcher.UIThread.Post(InvalidateVisual);
    }

    private void OnFrameMetrics(FrameMetric[] metrics)
    {
        if (_detaching) return;
        Dispatcher.UIThread.Post(() =>
        {
            if (_detaching) return;
            _latestMetrics = metrics;
            FrameMetricsReceived?.Invoke(metrics);
        });
    }

    private void OnWindowDeactivated(object? sender, EventArgs e) => ReleaseCursor();

    private void CaptureCursor()
    {
        if (_cursorCaptured || Bounds.Width <= 0 || Bounds.Height <= 0) return;

        PixelPoint topLeft = this.PointToScreen(new Point(0, 0));
        PixelPoint bottomRight = this.PointToScreen(new Point(Bounds.Width, Bounds.Height));
        _captureCenter = new PixelPoint((topLeft.X + bottomRight.X) / 2, (topLeft.Y + bottomRight.Y) / 2);

        var rect = new Win32Rect(topLeft.X, topLeft.Y, bottomRight.X, bottomRight.Y);
        ClipCursor(ref rect);
        SetCursorPos(_captureCenter.X, _captureCenter.Y);
        Cursor = new Cursor(StandardCursorType.None);
        _cursorCaptured = true;
        CursorCapturedChanged?.Invoke(true);
    }

    private void ReleaseCursor()
    {
        if (!_cursorCaptured) return;
        _cursorCaptured = false;
        Cursor = Cursor.Default;
        ClipCursor(IntPtr.Zero);
        CursorCapturedChanged?.Invoke(false);
    }

    private PixelSize PixelSizeFor(Size logicalSize)
    {
        double scale = TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0;
        return new PixelSize((int)Math.Ceiling(logicalSize.Width * scale), (int)Math.Ceiling(logicalSize.Height * scale));
    }

    private static byte? AsciiOf(Key key) =>
        key is >= Key.A and <= Key.Z ? (byte)('A' + (key - Key.A)) : null;

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.Tab && _cursorCaptured)
        {
            ReleaseCursor();
            e.Handled = true;
            return;
        }
        if (AsciiOf(e.Key) is not { } ascii) return;
        if (_keysDown.Add(ascii)) _renderer?.Key(ascii, true);
        e.Handled = true;
    }

    protected override void OnKeyUp(KeyEventArgs e)
    {
        base.OnKeyUp(e);
        if (AsciiOf(e.Key) is not { } ascii) return;
        if (_keysDown.Remove(ascii)) _renderer?.Key(ascii, false);
        e.Handled = true;
    }

    protected override void OnLostFocus(FocusChangedEventArgs e)
    {
        base.OnLostFocus(e);
        ReleaseCursor();
        foreach (byte ascii in _keysDown) _renderer?.Key(ascii, false);
        _keysDown.Clear();
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        Focus();
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
        CaptureCursor();
        e.Handled = true;
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (!_cursorCaptured) return;
        PixelPoint screenPos = this.PointToScreen(e.GetPosition(this));
        int dx = screenPos.X - _captureCenter.X;
        int dy = screenPos.Y - _captureCenter.Y;
        if (dx == 0 && dy == 0) return;
        _renderer?.MouseMove(dx, dy);
        SetCursorPos(_captureCenter.X, _captureCenter.Y);
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly struct Win32Rect(int left, int top, int right, int bottom)
    {
        public readonly int Left = left;
        public readonly int Top = top;
        public readonly int Right = right;
        public readonly int Bottom = bottom;
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClipCursor(ref Win32Rect rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ClipCursor(IntPtr rect);
}
