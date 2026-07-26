module;
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>

module StaticMesh;

import vertex;
import Logger;
import boundingBox;

bool StaticMesh::load(std::filesystem::path AssetPath) {
    Assimp::Importer importer;

    std::string path = absolute(AssetPath).string();
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate        |
        aiProcess_GenSmoothNormals   |
        aiProcess_CalcTangentSpace   |
        aiProcess_PreTransformVertices |
        aiProcess_GenBoundingBoxes   |
        aiProcess_FlipUVs);

    if (!scene || !scene->HasMeshes()) {
        logError("Failed to load mesh from " + path + ": " + importer.GetErrorString());
        return false;
    }

    const std::filesystem::path dir = absolute(AssetPath).parent_path();
    models.clear();
    materialRegistry.clear();

    //Assimp material idx -> material
    std::unordered_map<unsigned int, Material*> matCache;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* aimesh = scene->mMeshes[m];

        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;

        for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
            float u = 0.0f, v = 0.0f;
            if (aimesh->HasTextureCoords(0)) {
                u = aimesh->mTextureCoords[0][i].x;
                v = aimesh->mTextureCoords[0][i].y;
            }

            float tx = 0.0f, ty = 0.0f, tz = 0.0f, bsign = 1.0f;
            if (aimesh->HasTangentsAndBitangents()) {
                const glm::vec3 n(aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z);
                const glm::vec3 t(aimesh->mTangents[i].x, aimesh->mTangents[i].y, aimesh->mTangents[i].z);
                const glm::vec3 b(aimesh->mBitangents[i].x, aimesh->mBitangents[i].y, aimesh->mBitangents[i].z);
                tx = t.x; ty = t.y; tz = t.z;
                bsign = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
            }

            const glm::vec3 p(aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z);
            vertices.push_back({ p.x, p.y, p.z,
                aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z,
                u, v, tx, ty, tz, bsign });
        }

        for (unsigned int i = 0; i < aimesh->mNumFaces; ++i)
            for (unsigned int j = 0; j < aimesh->mFaces[i].mNumIndices; ++j)
                indices.push_back(aimesh->mFaces[i].mIndices[j]);

        auto model = std::make_unique<Model>();
        model->uploadGeometry(vertices.data(),
                              static_cast<int>(vertices.size() * sizeof(Vertex)),
                              indices.data(),
                              static_cast<int>(indices.size()));

        BoundingBox bounds;
        bounds.min = glm::vec3(aimesh->mAABB.mMin.x, aimesh->mAABB.mMin.y, aimesh->mAABB.mMin.z);
        bounds.max = glm::vec3(aimesh->mAABB.mMax.x, aimesh->mAABB.mMax.y, aimesh->mAABB.mMax.z);
        model->setLocalBounds(bounds);
        model->setModelMatrix(modelMatrix);

        RenderMode meshMode = (renderMode == RenderMode::Auto) ? RenderMode::Deferred : renderMode;

        //Resolve materials
        Material* mat = nullptr;
        if (scene->HasMaterials() && aimesh->mMaterialIndex < scene->mNumMaterials) {
            auto it = matCache.find(aimesh->mMaterialIndex);
            if (it != matCache.end()) {
                mat = it->second; //This material was already there
            } else { //New material
                auto owned = std::make_unique<Material>();
                owned->loadFromAssimp(scene, scene->mMaterials[aimesh->mMaterialIndex],
                                       meshMode, renderMode);
                mat = owned.get();
                matCache[aimesh->mMaterialIndex] = mat;
                materialRegistry.push_back(std::move(owned));
            }
        }

        model->setMaterial(mat);
        model->setRenderMode(meshMode);
        models.push_back(std::move(model));
    }

    boundsDirty = true;
    return !models.empty();
}

// Transform

void StaticMesh::propagateMatrix() const
{
    for (auto& m : models)
        m->setModelMatrix(modelMatrix);
}


BoundingBox StaticMesh::getBounds() const {
    if (!boundsDirty) return cachedBounds;

    if (models.empty()) { cachedBounds = {}; boundsDirty = false; return cachedBounds; }

    BoundingBox result = models[0]->getWorldBounds();
    for (std::size_t i = 1; i < models.size(); ++i) {
        const BoundingBox b = models[i]->getWorldBounds();
        result.min = glm::min(result.min, b.min);
        result.max = glm::max(result.max, b.max);
    }
    cachedBounds = result;
    boundsDirty  = false;
    return cachedBounds;
}

void StaticMesh::setTransformMatrix(const glm::mat4& newTransformMatrix) {
    modelMatrix = newTransformMatrix;
    boundsDirty = true;
    propagateMatrix();
}

void StaticMesh::setPosition(const glm::vec3& pos) {
    modelMatrix[3] = glm::vec4(pos, 1.0f);
    boundsDirty = true;
    propagateMatrix();
}

void StaticMesh::setRotation(const glm::vec3& eulerRadians) {
    const glm::vec3 translation = glm::vec3(modelMatrix[3]);
    const glm::vec3 scale(
        glm::length(glm::vec3(modelMatrix[0])),
        glm::length(glm::vec3(modelMatrix[1])),
        glm::length(glm::vec3(modelMatrix[2]))
    );

    const glm::mat4 rot =
        glm::rotate(glm::mat4(1.0f), eulerRadians.y, glm::vec3(0, 1, 0))
      * glm::rotate(glm::mat4(1.0f), eulerRadians.x, glm::vec3(1, 0, 0))
      * glm::rotate(glm::mat4(1.0f), eulerRadians.z, glm::vec3(0, 0, 1));

    modelMatrix[0] = rot[0] * scale.x;
    modelMatrix[1] = rot[1] * scale.y;
    modelMatrix[2] = rot[2] * scale.z;
    modelMatrix[3] = glm::vec4(translation, 1.0f);
    boundsDirty = true;
    propagateMatrix();
}

void StaticMesh::setRotation(const glm::quat& q) {
    const glm::vec3 translation = glm::vec3(modelMatrix[3]);
    const glm::vec3 scale(
        glm::length(glm::vec3(modelMatrix[0])),
        glm::length(glm::vec3(modelMatrix[1])),
        glm::length(glm::vec3(modelMatrix[2]))
    );
    const glm::mat4 rot = glm::mat4_cast(q);
    modelMatrix[0] = rot[0] * scale.x;
    modelMatrix[1] = rot[1] * scale.y;
    modelMatrix[2] = rot[2] * scale.z;
    modelMatrix[3] = glm::vec4(translation, 1.0f);
    boundsDirty = true;
    propagateMatrix();
}

void StaticMesh::setScale(const glm::vec3& scale) {
    const glm::vec3 oldScale(
        glm::length(glm::vec3(modelMatrix[0])),
        glm::length(glm::vec3(modelMatrix[1])),
        glm::length(glm::vec3(modelMatrix[2]))
    );

    modelMatrix[0] = glm::vec4(glm::vec3(modelMatrix[0]) / oldScale.x * scale.x, 0.0f);
    modelMatrix[1] = glm::vec4(glm::vec3(modelMatrix[1]) / oldScale.y * scale.y, 0.0f);
    modelMatrix[2] = glm::vec4(glm::vec3(modelMatrix[2]) / oldScale.z * scale.z, 0.0f);
    boundsDirty = true;
    propagateMatrix();
}

glm::vec3 StaticMesh::getScale() const {
    return glm::vec3(
        glm::length(glm::vec3(modelMatrix[0])),
        glm::length(glm::vec3(modelMatrix[1])),
        glm::length(glm::vec3(modelMatrix[2]))
    );
}

glm::vec3 StaticMesh::getPosition() const {
    return glm::vec3(modelMatrix[3]);
}

glm::vec3 StaticMesh::getRotation() const {
    const glm::mat3 rot(
        glm::normalize(glm::vec3(modelMatrix[0])),
        glm::normalize(glm::vec3(modelMatrix[1])),
        glm::normalize(glm::vec3(modelMatrix[2]))
    );
    const float x = std::asin(-rot[2][1]);
    const float y = std::atan2(rot[2][0], rot[2][2]);
    const float z = std::atan2(rot[0][1], rot[1][1]);
    return glm::vec3(x, y, z);
}

void StaticMesh::setRenderMode(RenderMode newRenderMode) {
    renderMode = newRenderMode;
    if (newRenderMode == RenderMode::Auto) return; // Resolved per model
    for (auto& model : models)
        model->setRenderMode(newRenderMode);
}
