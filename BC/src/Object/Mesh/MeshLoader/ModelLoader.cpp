module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module ModelLoader;

import vulkan;
import Logger;

namespace fs = std::filesystem;

bool ModelLoader::init(BufferManager& buffers, BlockingTransferBatch& batch,
                       TextureManager& textures) {
    if (!buffers.initialized()) {
        logError("ModelLoader: BufferManager must be initialized first");
        return false;
    }
    buffers_  = &buffers;
    batch_    = &batch;
    textures_ = &textures;
    return true;
}

std::shared_ptr<MultiMesh> ModelLoader::loadModel(const fs::path& path) {
    if (path.extension() != PMMA_EXTENSION) {
        logError("ModelLoader: " + path.string() + " is not a " + PMMA_EXTENSION + ";");
        return nullptr;
    }

    PmmaAsset asset;
    if (!asset.read(path)) return nullptr;

    std::shared_ptr<MultiMesh> model = upload(asset.model());
    if (!model) return nullptr;

    const AssetModel view = asset.model();
    logMessage("ModelLoader: " + path.filename().string() + " -> one model, " +
               std::to_string(model->size()) + " parts, " +
               std::to_string(view.textures.size()) + " textures");
    return model;
}

std::shared_ptr<MultiMesh> ModelLoader::upload(const AssetModel& model) {
    if (!buffers_ || !batch_ || !textures_) {
        logError("ModelLoader: upload called before init");
        return nullptr;
    }
    if (model.empty()) {
        logError("ModelLoader: the model holds no parts");
        return nullptr;
    }
    BufferManager&         buffers  = *buffers_;
    BlockingTransferBatch& batch    = *batch_;
    TextureManager&        textures = *textures_;
    
    std::vector<uint32_t> slots(model.textures.size(), INVALID_TEXTURE_INDEX);
    for (size_t i = 0; i < model.textures.size(); ++i) {
        const AssetTexture& texture = model.textures[i];

        const vk::CommandBuffer cmd = batch.begin();
        if (!cmd) return nullptr;

        slots[i] = textures.add(buffers, cmd, texture.pixels,
                                static_cast<vk::Format>(texture.format.value),
                                vk::Extent2D{texture.width, texture.height},
                                texture.levelCount);
        if (!batch.submitAndWait()) slots[i] = INVALID_TEXTURE_INDEX;
        buffers.resetUpload();
    }

    auto slotOf = [&](const AssetTextureIndex& index) {
        return index ? slots[index.value] : INVALID_TEXTURE_INDEX;
    };

    std::vector<std::shared_ptr<Material>> materials;
    materials.reserve(model.materials.size());
    for (const AssetMaterial& source : model.materials) {
        auto material = std::make_shared<Material>();
        material->setAlbedo(source.albedo);
        material->setEmissiveColor(source.emissiveColor);
        material->setEmissiveIntensity(source.emissiveIntensity);
        material->setSpecularIntensity(source.specularIntensity);
        material->setShininess(source.shininess);
        material->setMetallic(source.metallic);
        material->setRoughness(source.roughness);
        material->setAlphaThreshold(source.alphaThreshold);
        material->setEmissive((source.flags & MATERIAL_EMISSIVE) != 0);
        material->setTransparent((source.flags & MATERIAL_ALPHA_BLEND) != 0);
        material->setAlbedoTexture(slotOf(source.textures[0]));
        material->setNormalTexture(slotOf(source.textures[1]));
        material->setORMTexture(slotOf(source.textures[2]));
        material->setEmissiveTexture(slotOf(source.textures[3]));
        materials.push_back(std::move(material));
    }

    /* Primitives with no material share this one. */
    std::shared_ptr<Material> defaultMaterial;

    std::vector<std::shared_ptr<Mesh>> meshes;
    meshes.reserve(model.meshes.size());
    for (const AssetMesh& source : model.meshes) {
        std::shared_ptr<Material> material;
        if (source.material)
            material = materials[source.material.value];
        else {
            if (!defaultMaterial) defaultMaterial = std::make_shared<Material>();
            material = defaultMaterial;
        }

        const vk::CommandBuffer cmd = batch.begin();
        if (!cmd) return nullptr;

        auto       mesh     = std::make_shared<Mesh>();
        const bool uploaded = mesh->upload(buffers, cmd, source.geometry,
                                           std::move(material));

        if (!batch.submitAndWait()) return nullptr;
        buffers.resetUpload();
        if (!uploaded) {
            logError("ModelLoader: upload failed for mesh " +
                     std::to_string(meshes.size()));
            meshes.push_back(nullptr);
            continue;
        }
        meshes.push_back(std::move(mesh));
    }

    auto result = std::make_shared<MultiMesh>();
    for (const AssetPart& part : model.parts) {
        if (const std::shared_ptr<Mesh>& mesh = meshes[part.mesh.value])
            result->add(mesh, part.transform);
    }

    if (result->empty()) {
        logError("ModelLoader: nothing uploaded, the model has no drawable parts");
        return nullptr;
    }
    return result;
}
