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
    const uint32_t meshlets = latest_.meshletsDrawn + latest_.meshletsCulled;
    const auto percent = [](uint32_t part, uint32_t whole) {
        return whole == 0 ? 0.0f : 100.0f * static_cast<float>(part) / static_cast<float>(whole);
    };
    const uint32_t instancesCulled = latest_.instances - latest_.instancesDrawn;

    ImGui::Text("objects   %7u drawn  %7u culled  of %7u, %5.1f%% culled", latest_.instancesDrawn,
                instancesCulled, latest_.instances, percent(instancesCulled, latest_.instances));
    ImGui::Text("meshlets  %7u drawn  %7u culled  of %7u, %5.1f%% culled", latest_.meshletsDrawn,
                latest_.meshletsCulled, meshlets, percent(latest_.meshletsCulled, meshlets));
    ImGui::Text("triangles %7u drawn  of %7u submitted, %5.1f%% culled", latest_.trianglesDrawn,
                latest_.trianglesSubmitted,
                percent(latest_.trianglesSubmitted - latest_.trianglesDrawn, latest_.trianglesSubmitted));
}
