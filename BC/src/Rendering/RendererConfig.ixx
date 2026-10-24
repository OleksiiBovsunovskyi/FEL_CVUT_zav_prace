export module RendererConfig;

import vulkan;
export import Resource;

/// Format of every depth attachment.
export constexpr vk::Format DEPTH_FORMAT = vk::Format::eD32Sfloat;

static_assert(DEPTH_FORMAT == vk::Format::eD32Sfloat ||
              DEPTH_FORMAT == vk::Format::eD32SfloatS8Uint,
              "reverse-Z needs a float depth format; a UNORM one distributes "
              "precision evenly and gains nothing from the flip");

/// Layout shared depth targets are sampled in after their pass.
export constexpr vk::ImageLayout SAMPLED_DEPTH_LAYOUT = vk::ImageLayout::eShaderReadOnlyOptimal;

/// Layout the depth pyramid is built and sampled in.
export constexpr vk::ImageLayout DEPTH_PYRAMID_LAYOUT = vk::ImageLayout::eGeneral;

// Buffer accesses.

export constexpr ResourceAccess TRANSFER_WRITE{vk::PipelineStageFlagBits2::eAllTransfer,
                                               vk::AccessFlagBits2::eTransferWrite};
export constexpr ResourceAccess COMPUTE_SCATTER_WRITE{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite};
export constexpr ResourceAccess COMPUTE_READ{vk::PipelineStageFlagBits2::eComputeShader,
                                             vk::AccessFlagBits2::eShaderStorageRead};
export constexpr ResourceAccess COMPUTE_WRITE{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead |
        vk::AccessFlagBits2::eShaderStorageWrite};
export constexpr ResourceAccess MESH_SHADER_READ{
    vk::PipelineStageFlagBits2::eMeshShaderEXT,
    vk::AccessFlagBits2::eShaderStorageRead};
export constexpr ResourceAccess INDIRECT_READ{vk::PipelineStageFlagBits2::eDrawIndirect,
                                              vk::AccessFlagBits2::eIndirectCommandRead};

// Image accesses.

/// Shared depth target sampled by build_depth_pyramid.
export constexpr ResourceAccess DEPTH_SAMPLED_COMPUTE_READ{
    .stageMask  = vk::PipelineStageFlagBits2::eComputeShader,
    .accessMask = vk::AccessFlagBits2::eShaderSampledRead,
    .layout     = SAMPLED_DEPTH_LAYOUT};

/// Shared depth target tested and written by the forward pass.
export constexpr ResourceAccess DEPTH_ATTACHMENT_READ_WRITE{
    .stageMask  = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                  vk::PipelineStageFlagBits2::eLateFragmentTests,
    .accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                  vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    .layout     = vk::ImageLayout::eDepthAttachmentOptimal};

/// Depth pyramid sampled for occlusion culling.
export constexpr ResourceAccess DEPTH_PYRAMID_SAMPLED_READ{
    .stageMask  = vk::PipelineStageFlagBits2::eComputeShader |
                  vk::PipelineStageFlagBits2::eMeshShaderEXT |
                  vk::PipelineStageFlagBits2::eFragmentShader,
    .accessMask = vk::AccessFlagBits2::eShaderSampledRead,
    .layout     = DEPTH_PYRAMID_LAYOUT};

/// Depth pyramid read and written as storage by build_depth_pyramid.
export constexpr ResourceAccess DEPTH_PYRAMID_COMPUTE_READ_WRITE{
    .stageMask  = vk::PipelineStageFlagBits2::eComputeShader,
    .accessMask = vk::AccessFlagBits2::eShaderStorageRead |
                  vk::AccessFlagBits2::eShaderStorageWrite,
    .layout     = DEPTH_PYRAMID_LAYOUT};

/// Texture filled by a buffer-to-image copy.
export constexpr ResourceAccess TEXTURE_TRANSFER_WRITE{
    .stageMask  = vk::PipelineStageFlagBits2::eTransfer,
    .accessMask = vk::AccessFlagBits2::eTransferWrite,
    .layout     = vk::ImageLayout::eTransferDstOptimal};

/// Texture sampled by fragment shaders.
export constexpr ResourceAccess TEXTURE_FRAGMENT_SAMPLED_READ{
    .stageMask  = vk::PipelineStageFlagBits2::eFragmentShader,
    .accessMask = vk::AccessFlagBits2::eShaderSampledRead,
    .layout     = vk::ImageLayout::eShaderReadOnlyOptimal};
