module;
#include "pgr.h"
#include <vector>

export module Model;
export import Material;
export import RenderMode;

import boundingBox;

/**
 * Class that stores geometry.
 * One model may contain only one material.
 */
export class Model {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    int indexCount = 0;
    

    glm::mat4 modelMatrix{1.0f};
    glm::mat4 prevModelMatrix{1.0f};
    RenderMode renderMode = RenderMode::Deferred;
    Material* material = nullptr;
    BoundingBox localBounds;

public:
    Model() = default;
    ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    /**
     * Uploads model geometry to the GPU
     * @param vertexData vertex data
     * @param vertexBytes size of the vertex data in bytes
     * @param indices indices array
     * @param indexCount amount of indices
     */
    void uploadGeometry(const void* vertexData, int vertexBytes,
                        const unsigned int* indices, int indexCount);

    /**
     * Draws geometry associated with this model
     */
    void drawGeometry() const;

    void setLocalBounds(const BoundingBox& b) { localBounds = b; }
    void setModelMatrix(const glm::mat4& m) { modelMatrix = m; }

    [[nodiscard]] glm::mat4 getModelMatrix()     const { return modelMatrix; }
    [[nodiscard]] glm::mat4 getPrevModelMatrix() const { return prevModelMatrix; }

    void advancePrevModelMatrix() { prevModelMatrix = modelMatrix; }

    void setMaterial(Material* m) { material = m; }

    [[nodiscard]] const Material& getMaterial() const { return *material; }

    /**
     * Updates render mode
     * - Defered/Forward/Auto
     * @param mode new render mode
     */
    void setRenderMode(RenderMode mode);

    [[nodiscard]] RenderMode getRenderMode() const { return renderMode; }


    [[nodiscard]] BoundingBox getBoundsUnscaled() const { return localBounds; }
    [[nodiscard]] BoundingBox getWorldBounds() const;
    
    [[nodiscard]] int getIndexCount() const { return indexCount; }
};
