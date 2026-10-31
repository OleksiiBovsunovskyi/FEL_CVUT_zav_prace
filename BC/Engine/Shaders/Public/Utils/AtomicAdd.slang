module AtomicAdd;

/// Adds `value` to `*target` as one atomic operation.
/// @return the value `*target` held before the add.
public uint atomicAdd(uint* target, uint value) {
    return ((Atomic<uint>*)target)->add(value);
}
