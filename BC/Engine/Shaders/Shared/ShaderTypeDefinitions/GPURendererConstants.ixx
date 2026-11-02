module;
#include <cstdint>
export module GPURendererConstants;

export constexpr uint32_t MAX_VERTICES  = 64;
export constexpr uint32_t MAX_TRIANGLES = 64;
/// Visibility-buffer drawIndex of a texel no triangle covers.
export constexpr uint32_t VISIBILITY_CLEAR = 0xFFFFFFFF;