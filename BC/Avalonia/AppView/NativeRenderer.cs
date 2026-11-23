using System.Runtime.InteropServices;

namespace AppView;

/// <summary>One named per-frame measurement published by the engine.</summary>
public readonly record struct FrameMetric(string Name, double Value);

internal readonly record struct ExternalSlotHandles(
    IntPtr MemoryHandle,
    ulong MemorySize,
    ulong MemoryOffset,
    IntPtr RenderDoneSemaphoreHandle,
    IntPtr AvaloniaDoneSemaphoreHandle);

/// <summary>
/// Manages one native app thread and its Presentation Bridge C ABI callbacks.
/// </summary>
internal sealed class NativeRenderer : IDisposable
{
    private const string Library = "app";

    private IntPtr _handle;

    // Pinned delegate references kept alive for the lifetime of the native worker thread.
    private readonly TargetsCreatedCallback _onTargetsCreated;
    private readonly TargetsDestroyedCallback _onTargetsDestroyed;
    private readonly PresentCallback _onPresent;
    private readonly ErrorCallback _onError;
    private readonly MetricsCallback _onMetrics;

    static NativeRenderer()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeRenderer).Assembly, (name, _, _) =>
            name == Library ? NativeLibrary.Load(Path.Combine(RendererView.ResolveDataRoot(), Library+".dll")) : IntPtr.Zero);
    }

    public unsafe NativeRenderer(
        string dataRoot,
        uint width,
        uint height,
        byte[]? deviceUuid,
        Action<ExternalSlotHandles[], int, int> onTargetsCreated,
        Action onTargetsDestroyed,
        Action<uint> onPresent,
        Action<string> onError,
        Action<FrameMetric[]> onMetrics)
    {
        _onTargetsCreated = (_, targetsPtr) =>
        {
            if (targetsPtr == IntPtr.Zero) return;
            EngineTargetSet targetSet = *(EngineTargetSet*)targetsPtr;
            var slots = new ExternalSlotHandles[targetSet.SlotCount];
            var nativeSlots = (EngineExternalSlotHandles*)targetSet.Slots;
            for (int i = 0; i < slots.Length; i++)
            {
                EngineExternalSlotHandles s = nativeSlots[i];
                slots[i] = new ExternalSlotHandles(
                    s.MemoryHandle,
                    s.MemorySize,
                    s.MemoryOffset,
                    s.RenderDoneSemaphoreHandle,
                    s.AvaloniaDoneSemaphoreHandle);
            }
            onTargetsCreated(slots, (int)targetSet.Width, (int)targetSet.Height);
        };
        _onTargetsDestroyed = _ => onTargetsDestroyed();
        _onPresent = (_, slotIndex) => onPresent(slotIndex);
        _onError = (_, messagePtr) => onError(Marshal.PtrToStringUTF8(messagePtr) ?? "unknown native error");
        _onMetrics = (_, metricsPtr) =>
        {
            if (metricsPtr == IntPtr.Zero) return;
            EngineFrameMetrics frame = *(EngineFrameMetrics*)metricsPtr;
            if (frame.Count == 0 || frame.Entries == IntPtr.Zero) return;
            var entries = new FrameMetric[frame.Count];
            var nativeEntries = (EngineMetricEntry*)frame.Entries;
            for (int i = 0; i < entries.Length; i++)
            {
                EngineMetricEntry entry = nativeEntries[i];
                entries[i] = new FrameMetric(
                    Marshal.PtrToStringUTF8(entry.Name) ?? string.Empty,
                    entry.Value);
            }
            onMetrics(entries);
        };

        IntPtr rootUtf8 = Marshal.StringToCoTaskMemUTF8(dataRoot);
        try
        {
            fixed (byte* uuidPtr = deviceUuid)
            {
                var config = new AppStartConfig
                {
                    DataRoot = rootUtf8,
                    Width = width,
                    Height = height,
                    DeviceUuid = deviceUuid is { Length: 16 } ? (IntPtr)uuidPtr : IntPtr.Zero,
                    Callbacks = new EngineHostCallbacks
                    {
                        UserData = IntPtr.Zero,
                        OnTargetsCreated = Marshal.GetFunctionPointerForDelegate(_onTargetsCreated),
                        OnTargetsDestroyed = Marshal.GetFunctionPointerForDelegate(_onTargetsDestroyed),
                        OnPresent = Marshal.GetFunctionPointerForDelegate(_onPresent),
                        OnError = Marshal.GetFunctionPointerForDelegate(_onError),
                        OnMetrics = Marshal.GetFunctionPointerForDelegate(_onMetrics),
                    }
                };

                _handle = Start(ref config);
                if (_handle == IntPtr.Zero)
                    throw new InvalidOperationException(LastError());
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(rootUtf8);
        }
    }

    public void ReleaseSlot(uint slotIndex)
    {
        IntPtr handle = _handle;
        if (handle != IntPtr.Zero) ReleaseSlot(handle, slotIndex);
    }

    public void Resize(uint width, uint height)
    {
        IntPtr handle = _handle;
        if (handle != IntPtr.Zero) Resize(handle, width, height);
    }

    public void Key(byte ascii, bool down)
    {
        IntPtr handle = _handle;
        if (handle != IntPtr.Zero) Key(handle, ascii, down ? 1 : 0);
    }

    public void MouseMove(float dx, float dy)
    {
        IntPtr handle = _handle;
        if (handle != IntPtr.Zero) MouseMove(handle, dx, dy);
    }

    public void Dispose()
    {
        IntPtr handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle == IntPtr.Zero) return;
        Stop(handle);
    }

    public static string LastError() => Marshal.PtrToStringUTF8(LastErrorPtr()) ?? "unknown error";

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void TargetsCreatedCallback(IntPtr userData, IntPtr targets);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void TargetsDestroyedCallback(IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void PresentCallback(IntPtr userData, uint slotIndex);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void ErrorCallback(IntPtr userData, IntPtr message);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void MetricsCallback(IntPtr userData, IntPtr metrics);

    [StructLayout(LayoutKind.Sequential)]
    private struct EngineMetricEntry
    {
        public IntPtr Name;
        public double Value;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EngineFrameMetrics
    {
        public uint Count;
        public IntPtr Entries;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EngineExternalSlotHandles
    {
        public IntPtr MemoryHandle;
        public ulong MemorySize;
        public ulong MemoryOffset;
        public IntPtr RenderDoneSemaphoreHandle;
        public IntPtr AvaloniaDoneSemaphoreHandle;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EngineTargetSet
    {
        public uint SlotCount;
        public uint Width;
        public uint Height;
        public IntPtr Slots;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct EngineHostCallbacks
    {
        public IntPtr UserData;
        public IntPtr OnTargetsCreated;
        public IntPtr OnTargetsDestroyed;
        public IntPtr OnPresent;
        public IntPtr OnError;
        public IntPtr OnMetrics;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct AppStartConfig
    {
        public IntPtr DataRoot;
        public uint Width;
        public uint Height;
        public IntPtr DeviceUuid;
        public EngineHostCallbacks Callbacks;
    }

    [DllImport(Library, EntryPoint = "app_start", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr Start(ref AppStartConfig config);

    [DllImport(Library, EntryPoint = "engine_stop", CallingConvention = CallingConvention.Cdecl)]
    private static extern void Stop(IntPtr app);

    [DllImport(Library, EntryPoint = "engine_release_slot", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ReleaseSlot(IntPtr app, uint slotIndex);

    [DllImport(Library, EntryPoint = "engine_resize", CallingConvention = CallingConvention.Cdecl)]
    private static extern void Resize(IntPtr app, uint width, uint height);

    [DllImport(Library, EntryPoint = "engine_key", CallingConvention = CallingConvention.Cdecl)]
    private static extern void Key(IntPtr app, byte ascii, int down);

    [DllImport(Library, EntryPoint = "engine_mouse_move", CallingConvention = CallingConvention.Cdecl)]
    private static extern void MouseMove(IntPtr app, float dx, float dy);

    [DllImport(Library, EntryPoint = "engine_last_error", CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr LastErrorPtr();
}
