module;
#include <IL/il.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <cstring>
#include <assimp/scene.h>
#include <assimp/material.h>
#include "pgr.h"
module Material;
import Logger.gl;
import Model;

Material::~Material() {
    if (materialUBOHandle) glDeleteBuffers(1, &materialUBOHandle);
    if (albedoTexture)     glDeleteTextures(1, &albedoTexture);
    if (normalTexture)     glDeleteTextures(1, &normalTexture);
    if (ormTexture)        glDeleteTextures(1, &ormTexture);
    if (emissiveTexture)   glDeleteTextures(1, &emissiveTexture);
}

static bool uploadILImageToGL(GLuint& tex) {
    ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);

    if (tex) glDeleteTextures(1, &tex);

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA,
                 ilGetInteger(IL_IMAGE_WIDTH), ilGetInteger(IL_IMAGE_HEIGHT),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, ilGetData());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

static bool loadFromMemory(const void* data, int bytes, GLuint& tex) {
    ILuint image;
    ilGenImages(1, &image);
    ilBindImage(image);
    if (!ilLoadL(IL_TYPE_UNKNOWN, data, static_cast<ILuint>(bytes))) {
        std::cerr << "Material: failed to load texture from memory\n";
        ilDeleteImages(1, &image);
        return false;
    }
    uploadILImageToGL(tex);
    ilDeleteImages(1, &image);
    return true;
}

bool Material::loadTextureFromMemory(const void* data, int bytes)       { return loadFromMemory(data, bytes, albedoTexture); }
bool Material::loadNormalTextureFromMemory(const void* data, int bytes) { return loadFromMemory(data, bytes, normalTexture); }
bool Material::loadEmissiveTextureFromMemory(const void* data, int bytes) { return loadFromMemory(data, bytes, emissiveTexture); }

bool Material::loadORMTextureFromMemory(const void* ormData, int ormBytes, const void* aoData, int aoBytes) {
    if (!ormData && !aoData) return false;

    ILuint img;
    ilGenImages(1, &img);
    ilBindImage(img);

    int w = 0, h = 0;
    std::vector<uint8_t> pixels;

    if (ormData) {
        if (!ilLoadL(IL_TYPE_UNKNOWN, ormData, static_cast<ILuint>(ormBytes))) {
            ilDeleteImages(1, &img);
            return false;
        }
        ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
        w = ilGetInteger(IL_IMAGE_WIDTH);
        h = ilGetInteger(IL_IMAGE_HEIGHT);
        pixels.resize(w * h * 4);
        std::memcpy(pixels.data(), ilGetData(), pixels.size());
    }

    ilDeleteImages(1, &img);

    if (aoData) {
        ILuint aoImg;
        ilGenImages(1, &aoImg);
        ilBindImage(aoImg);
        if (ilLoadL(IL_TYPE_UNKNOWN, aoData, static_cast<ILuint>(aoBytes))) {
            ilConvertImage(IL_RGBA, IL_UNSIGNED_BYTE);
            int aw = ilGetInteger(IL_IMAGE_WIDTH);
            int ah = ilGetInteger(IL_IMAGE_HEIGHT);
            const uint8_t* aoPixels = ilGetData();

            if (pixels.empty()) {
                w = aw; h = ah;
                pixels.resize(w * h * 4);
                for (int i = 0; i < w * h; i++) {
                    pixels[i * 4 + 0] = aoPixels[i * 4 + 0];
                    pixels[i * 4 + 1] = 255;
                    pixels[i * 4 + 2] = 0;
                    pixels[i * 4 + 3] = 255;
                }
            } else if (aw == w && ah == h) {
                for (int i = 0; i < w * h; i++)
                    pixels[i * 4 + 0] = aoPixels[i * 4 + 0];
            }
        }
        ilDeleteImages(1, &aoImg);
    }

    if (ormTexture) glDeleteTextures(1, &ormTexture);
    glGenTextures(1, &ormTexture);
    glBindTexture(GL_TEXTURE_2D, ormTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void Material::bindUniforms(GLuint /*shader*/) const {
    if (!materialUBOHandle) {
        MaterialUBO data{};
        data.albedo            = albedo;
        data.emissiveColor     = emissiveColor;
        data.emissiveIntensity = emissiveIntensity;
        data.specularIntensity  = specularIntensity;
        data.shininess          = shininess;
        data.metallic           = metallic;
        data.roughness          = roughness;
        data.alphaThreshold     = alphaThreshold;
        data.emissive           = emissive ? 1 : 0;
        data.useTexture         = albedoTexture   ? 1 : 0;
        data.useNormalMap       = normalTexture   ? 1 : 0;
        data.useORMMap          = ormTexture      ? 1 : 0;
        data.useEmissiveTexture = emissiveTexture ? 1 : 0;

        glGenBuffers(1, &materialUBOHandle);
        glBindBuffer(GL_UNIFORM_BUFFER, materialUBOHandle);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(MaterialUBO), &data, GL_STATIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    glBindBufferBase(GL_UNIFORM_BUFFER, 1, materialUBOHandle);

    if (albedoTexture)   { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, albedoTexture); }
    if (normalTexture)   { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normalTexture); }
    if (ormTexture)      { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ormTexture); }
    if (emissiveTexture) { glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, emissiveTexture); }
}

bool Material::loadFromAssimp(const aiScene* scene, const aiMaterial* mat, RenderMode& meshMode, RenderMode staticMeshMode) {
    aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
    mat->Get(AI_MATKEY_BASE_COLOR, baseColor);
    setAlbedo(glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a));

    // Alpha mode
    aiString alphaModeStr;
    if (mat->Get("$mat.gltf.alphaMode", 0, 0, alphaModeStr) == AI_SUCCESS) {
        const std::string alphaMode = alphaModeStr.C_Str();
        if (alphaMode == "BLEND") {
            setTransparent(true);
            if (staticMeshMode == RenderMode::Auto) meshMode = RenderMode::Forward;
        } else if (alphaMode == "MASK") {
            float cutoff = 0.5f;
            mat->Get("$mat.gltf.alphaCutoff", 0, 0, cutoff);
            setAlphaThreshold(cutoff);
        }
    }

    auto loadTex = [&](aiTextureType type, bool (Material::*fromMem)(const void*, int)) -> bool {
        aiString texPath;
        if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS) return false;
        const char* p = texPath.C_Str();
        if (p[0] != '*') return false;
        const int idx = std::atoi(p + 1);
        if (idx < 0 || idx >= (int)scene->mNumTextures) return false;
        const aiTexture* tex = scene->mTextures[idx];
        return (this->*fromMem)(tex->pcData, static_cast<int>(tex->mWidth));
    };

    loadTex(aiTextureType_BASE_COLOR, &Material::loadTextureFromMemory);
    loadTex(aiTextureType_NORMALS,    &Material::loadNormalTextureFromMemory);

    //Build ORM
    auto getEmbeddedTex = [&](aiTextureType type) -> const aiTexture* {
        aiString texPath;
        if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS) return nullptr;
        const char* p = texPath.C_Str();
        if (p[0] != '*') return nullptr;
        const int idx = std::atoi(p + 1);
        if (idx < 0 || idx >= (int)scene->mNumTextures) return nullptr;
        return scene->mTextures[idx];
    };

    const aiTexture* ormTex = getEmbeddedTex(aiTextureType_METALNESS);
    const aiTexture* aoTex  = getEmbeddedTex(aiTextureType_LIGHTMAP);
    if (ormTex || aoTex) {
        loadORMTextureFromMemory(
            ormTex ? ormTex->pcData : nullptr, ormTex ? static_cast<int>(ormTex->mWidth) : 0,
            aoTex  ? aoTex->pcData  : nullptr, aoTex  ? static_cast<int>(aoTex->mWidth)  : 0
        );
    }

    float roughnessFactor = 0.5f;
    mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);
    setRoughness(roughnessFactor);

    float metallicFactor = 0.0f;
    mat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);
    setMetallic(metallicFactor);

    // Emissive texture
    loadTex(aiTextureType_EMISSIVE, &Material::loadEmissiveTextureFromMemory);

    // Emissive factor
    aiColor3D emissiveFactor(0.0f, 0.0f, 0.0f);
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveFactor) == AI_SUCCESS) {
        setEmissiveColor(glm::vec3(emissiveFactor.r, emissiveFactor.g, emissiveFactor.b));
    }

    // Emissive strength
    float emissiveStrength = 1.0f;
    if (mat->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveStrength) == AI_SUCCESS) {
        setEmissiveIntensity(emissiveStrength);
    }

    return true;
}
