module;
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
export module Instance;

import GPUCoreTypes;
export import Mesh;

/**
 * Drawable instance
 * World position is position + rotation * (scale * mesh space position).
 * Scale is a half float per axis.
 * Rotation is a unit quaternion stored as its smallest three components [x y z w order, largest skipped],
 * 15 bits each over -1/sqrt(2)..1/sqrt(2); the largest is rebuilt as positive.
 ***/
export struct Instance {
    glm::vec3 position{};          // world space
    uint32_t scaleX : 16;          // half
    uint32_t scaleY : 16;          // half
    uint32_t scaleZ : 16;          // half
    uint32_t rotationA : 15;
    uint32_t reserved : 1;
    uint32_t rotationB : 15;
    uint32_t rotationC : 15;
    uint32_t rotationLargest : 2;  // index of the dropped component, x y z w
    GpuPtr<Mesh> mesh{};

    void packScale(glm::vec3 scale) {
        scaleX = glm::packHalf1x16(scale.x);
        scaleY = glm::packHalf1x16(scale.y);
        scaleZ = glm::packHalf1x16(scale.z);
    }
    [[nodiscard]] glm::vec3 unpackScale() const {
        return { glm::unpackHalf1x16(static_cast<uint16_t>(scaleX)),
                 glm::unpackHalf1x16(static_cast<uint16_t>(scaleY)),
                 glm::unpackHalf1x16(static_cast<uint16_t>(scaleZ)) };
    }

    /// @param rotation rotation quaternion, normalized before packing
    void packRotation(glm::quat rotation) {
        rotation = glm::normalize(rotation);
        glm::vec4 c(rotation.x, rotation.y, rotation.z, rotation.w);
        int largest = 0;
        for (int i = 1; i < 4; ++i) {
            if (glm::abs(c[i]) > glm::abs(c[largest])) largest = i;
        }
        if (c[largest] < 0.0f) c = -c;
        uint32_t stored[3];
        for (int i = 0, n = 0; i < 4; ++i) {
            if (i != largest) stored[n++] = encodeComponent(c[i]);
        }
        rotationA = stored[0];
        rotationB = stored[1];
        rotationC = stored[2];
        rotationLargest = static_cast<uint32_t>(largest);
    }
    /// @return unit rotation quaternion.
    [[nodiscard]] glm::quat unpackRotation() const {
        const float stored[3] = { decodeComponent(rotationA), decodeComponent(rotationB), decodeComponent(rotationC) };
        glm::vec4 c;
        for (int i = 0, n = 0; i < 4; ++i) {
            c[i] = i == static_cast<int>(rotationLargest) ? 0.0f : stored[n++];
        }
        c[rotationLargest] = glm::sqrt(glm::max(0.0f, 1.0f - glm::dot(c, c)));
        return glm::normalize(glm::quat(c.w, c.x, c.y, c.z));
    }

private:
    /// @return 15-bit value of a quaternion component in -1/sqrt(2)..1/sqrt(2).
    static uint32_t encodeComponent(float value) {
        float unit = glm::clamp(value * glm::root_two<float>() * 0.5f + 0.5f, 0.0f, 1.0f);
        return static_cast<uint32_t>(glm::round(unit * 32767.0f));
    }
    static float decodeComponent(uint32_t q) {
        return (static_cast<float>(q) / 32767.0f * 2.0f - 1.0f) / glm::root_two<float>();
    }
};

static_assert(sizeof(Instance) == 32);

/**
 * One instance write
 * instances[index] = instance.
 ***/
export struct InstanceUpdate {
    uint32_t index = 0;
    uint32_t reserved = 0;
    Instance instance{};
};

static_assert(sizeof(InstanceUpdate) == 40);