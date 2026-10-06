module;
#include "imgui.h"

#include <cstdint>
#include <optional>

module GpuDrawStats;

import vulkan;
import Metrics;
import VkUtil;

bool GpuDrawStats::init(VulkanContext& ctx) {
    return records_.init(ctx, "draw counters");
}

void GpuDrawStats::destroy() {
    records_.destroy();
    written_.fill(false);
}

GpuPtr<GPUDrawCounters> GpuDrawStats::beginFrame(Frame::Recording& recording,
                                                 uint32_t instanceCount) {
    const FrameInFlightIndex frameInFlight = recording.frameInFlight();
    bool& written = recording.select(written_);

    if (written) {
        if (const std::optional<GPUDrawCounters> counters = records_.read(frameInFlight)) {
            latest_ = *counters;

            Metrics& metrics = Metrics::get();
            metrics.set("instances", latest_.instances);
            metrics.set("instances_drawn", latest_.instancesDrawn);
            metrics.set("instances_culled", latest_.instances - latest_.instancesDrawn);
            metrics.set("meshlets_drawn", latest_.meshletsDrawn);
            metrics.set("meshlets_culled", latest_.meshletsCulled);
            metrics.set("triangles_submitted", latest_.trianglesSubmitted);
            metrics.set("triangles_drawn", latest_.trianglesDrawn);
        }
    }

    const GpuPtr<GPUDrawCounters> address =
        records_.write(frameInFlight, GPUDrawCounters{.instances = instanceCount});
    written = address.address != 0;
    return address;
}

void GpuDrawStats::endFrame(Frame::Recording& recording) {
    barrier(recording.commandBuffer(),
            vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eMeshShaderEXT,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead);
}

void GpuDrawStats::drawUI() const {
    const uint32_t meshlets =
        latest_.meshletsDrawn + latest_.meshletsCulled + latest_.meshletsOccluded;

    const auto percent = [](uint32_t part, uint32_t whole) {
        return whole != 0 ? 100.0 * part / whole : 0.0;
    };
    const uint32_t instancesCulled  = latest_.instances - latest_.instancesDrawn;
    const uint32_t trianglesCulled  = latest_.trianglesSubmitted - latest_.trianglesDrawn;

    ImGui::Text("objects   %7u drawn  %7u culled (%.1f%%)  of %7u", latest_.instancesDrawn,
                instancesCulled, percent(instancesCulled, latest_.instances), latest_.instances);
    ImGui::Text("occluded  %7u (%.1f%% of objects)", latest_.instancesOccluded,
                percent(latest_.instancesOccluded, latest_.instances));
    ImGui::Text("meshlets  %7u drawn  %7u culled (%.1f%%)  %7u occluded (%.1f%%)  of %7u",
                latest_.meshletsDrawn, latest_.meshletsCulled,
                percent(latest_.meshletsCulled, meshlets), latest_.meshletsOccluded,
                percent(latest_.meshletsOccluded, meshlets), meshlets);
    ImGui::Text("triangles %7u drawn  %7u culled (%.1f%%)  of %7u submitted",
                latest_.trianglesDrawn, trianglesCulled,
                percent(trianglesCulled, latest_.trianglesSubmitted), latest_.trianglesSubmitted);
}
