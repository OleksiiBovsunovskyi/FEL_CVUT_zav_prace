module draw_meshlets;

/// Flat debug colour for a meshlet.
///
/// @param id value carried by MeshVertex::meshletId.
public float3 meshletColor(uint id) {
    // Wang hash; neighbouring ids land on unrelated colours.
    uint h = (id ^ 61u) ^ (id >> 16);
    h *= 9u;
    h ^= h >> 4;
    h *= 0x27d4eb2du;
    h ^= h >> 15;
    // The 0.25 floor keeps every channel out of near-black.
    return 0.25 + 0.75 * float3(h & 0xffu, (h >> 8) & 0xffu, (h >> 16) & 0xffu) / 255.0;
}
