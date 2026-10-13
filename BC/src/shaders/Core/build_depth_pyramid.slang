import gpu_types;
import AtomicAdd;

/*
* We dont have to copy depth texture here, since it is already there, but for simplicity, it is done...
* Goal of this shader is to build depth pyramid in one dispatch
* We split depth texture into 64*64 blocks (TILE_SIDE), then each workgroup builds mips for it. 
* If this was not enough, last workgroup build remaining mips 
* (the resolution there shouldnt be that big, so one workgroup should be more than wee need)
*/

[[vk_binding(0, 0)]] Sampler2D textures[];
[[vk_binding(1, 0)]] globallycoherent RWTexture2D<float> storageImages[];


[[vk_push_constant]] ConstantBuffer<GPUDepthPyramidPush> pc;

static const uint THREADS = 256;
/// Size of region this workgroup owns
static const uint TILE_SIDE = 64; //(int)pow(2, 6);
/// Mips one workgroup builds inside its region.
static const uint TILE_MIPS = 7; //log2(TILE_SIDE) + 1

groupshared bool isLastGroup;

/// Texel coordinates [first, end -1] on one axis.
struct TexelRange {
    uint first;
    uint end;
};

/*
 * @param pyramidSize size of the whole pyramid
 * @param level mip level we are working with   
 * @return size of mip level
*/
uint2 mipSize(uint2 pyramidSize, uint level) {
    return max(pyramidSize >> level, uint2(1, 1));
}

/**
  * Helper to handle odd size case
  * @param dstTexel texel where we want to write
  * @param dstSize size of the destination mip on axis
  * @param srcSize size of the source mip on axis
  * @return source range in format [first, end-1].
 */
TexelRange sourceRangeOnAxis(uint dstTexel, uint dstSize, uint srcSize) {
    TexelRange range;
    range.first = dstTexel * 2;
    range.end   = dstTexel == dstSize - 1 ? srcSize : range.first + 2;
    return range;
}

/*
 * Computes lowest depth at the level for given texel
 * @param level mip level we are building
 * @param dstTexel texel we are building
 * @param pyramidSize size of the pyramid`s mip 0
*/
float farthestBelow(uint level, uint2 dstTexel, uint2 pyramidSize) {
    const uint2 dstSize = mipSize(pyramidSize, level);
    const uint2 srcSize = mipSize(pyramidSize, level - 1);
    const TexelRange columns = sourceRangeOnAxis(dstTexel.x, dstSize.x, srcSize.x);
    const TexelRange rows    = sourceRangeOnAxis(dstTexel.y, dstSize.y, srcSize.y);

    float farthest = 1.0f;
    for (uint y = rows.first; y < rows.end; ++y)
        for (uint x = columns.first; x < columns.end; ++x)
            farthest = min(farthest, storageImages[pc.pyramidMip0Slot + level - 1][uint2(x, y)]);
    return farthest;
}

/**
 * Writes mip level texels in [begin, end-1], then syncs.
 */
void buildRegion(uint level, uint2 begin, uint2 end, uint2 pyramidSize, uint thread) {
    const uint2 regionSize = end - begin;
    for (uint texelIndex = thread; texelIndex < regionSize.x * regionSize.y;
         texelIndex += THREADS) {
        const uint2 texel = begin + uint2(texelIndex % regionSize.x, texelIndex / regionSize.x);
        const float depth = level == 0 ? textures[pc.depthSlot].Load(int3(texel, 0)).r
                                       : farthestBelow(level, texel, pyramidSize);
        storageImages[pc.pyramidMip0Slot + level][texel] = depth;
    }
    DeviceMemoryBarrierWithGroupSync();
}

/**
 * Builds mip levels [firstLevel, endLevel - 1] of a tile.
 * @param tile tile index in the grid 
 * @param tilesPerAxis tiles per axis
 */
void buildTileMips(uint firstLevel, uint endLevel, uint2 tile, uint2 tilesPerAxis, uint2 pyramidSize,
                   uint thread) {
    const bool2 lastOnAxis = tile == tilesPerAxis - 1;
    for (uint level = firstLevel; level < endLevel; ++level) {
        const uint2 begin = tile * (TILE_SIDE >> level);
        const uint2 end   = select(lastOnAxis, mipSize(pyramidSize, level), begin + (TILE_SIDE >> level));
        buildRegion(level, begin, end, pyramidSize, thread);
    }
}

[shader("compute")]
[numthreads(THREADS, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint thread : SV_GroupIndex) {
    uint2 pyramidSize;
    storageImages[pc.pyramidMip0Slot].GetDimensions(pyramidSize.x, pyramidSize.y);
     
    const uint2 groupCount = max(pyramidSize / TILE_SIDE, uint2(1, 1));

    // Mips 0-6, one tile per workgroup.
    buildTileMips(0, min(TILE_MIPS, pc.mipCount), groupId.xy, groupCount, pyramidSize, thread);
    if (pc.mipCount <= TILE_MIPS) return;
    
    //We want to know if this is the last workgroup
    if (thread == 0) {
        const uint finishedBefore = atomicAdd(&pc.counter->finishedWorkgroups, 1); //We need some buffer shared between all the workgroups
        isLastGroup = finishedBefore == groupCount.x * groupCount.y - 1;
    }
    
    GroupMemoryBarrierWithGroupSync();
    if (!isLastGroup) return;
    //Here tex is small enough, so we can give it to just one workgroup 
    // Mips 7 and up
    buildTileMips(TILE_MIPS, pc.mipCount, uint2(0, 0), uint2(1, 1), pyramidSize, thread);

    //Resset the counter for next frame
    if (thread == 0) pc.counter->finishedWorkgroups = 0;
}
