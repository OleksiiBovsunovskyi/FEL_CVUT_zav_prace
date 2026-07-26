module;
#include <vector>
#include <memory>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
export module StaticMesh;
export import Model;
import boundingBox;


/**
 * Class representing one mesh.
 * Static mesh can consist of multiple models, but they act as one.
 */
export class StaticMesh {


public:
    StaticMesh() = default;
    StaticMesh(const StaticMesh&) = delete;
    StaticMesh& operator=(const StaticMesh&) = delete;
    
    /***
     * Loads static mesh from file
     * @param AssetPath Path to the file
     * @return true if loaded successfully, false otherwise
     */
    bool load(std::filesystem::path AssetPath);

    /**
     * Sets transformation matrix for this static mesh
     * @param newTransformMatrix new transformation matrix
     */
    void setTransformMatrix(const glm::mat4& newTransformMatrix);

    void setPosition(const glm::vec3& pos);
    void setRotation(const glm::vec3& eulerRadians);
    void setRotation(const glm::quat& q);
    void setScale(const glm::vec3& scale);
    
    [[nodiscard]] glm::mat4 getModelMatrix() const { return modelMatrix; }
    [[nodiscard]] glm::vec3 getPosition() const;
    [[nodiscard]] glm::vec3 getRotation() const;
    [[nodiscard]] glm::vec3 getScale() const;

    void setRenderMode(RenderMode newRenderMode);

    [[nodiscard]] RenderMode getRenderMode() const { return renderMode; }

    [[nodiscard]] BoundingBox getBounds() const;

    [[nodiscard]] int getModelCount() const { return static_cast<int>(models.size()); }

    /**
     * Returns model associated with this mesh by its index i
     * @param i index of the model to return
     * @return model at index i
     */
    [[nodiscard]] Model* getModel(int i = 0) const {
        return (i >= 0 && i < static_cast<int>(models.size())) ? models[i].get() : nullptr;
    }

    /**
     * Retruns all models associates with this mesh
     * @return vector of all models
     */
    [[nodiscard]] const std::vector<std::unique_ptr<Model>>& getAllModels() const { return models; }

private:

    std::vector<std::unique_ptr<Model>>    models;
    std::vector<std::unique_ptr<Material>> materialRegistry;
    glm::mat4 modelMatrix{1.0f};
    RenderMode renderMode = RenderMode::Deferred;

    mutable BoundingBox cachedBounds;
    mutable bool        boundsDirty = true;

    void propagateMatrix() const;
};
