module;
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module RenderComponent;

export import VkScene;
export import DrawList;
export import MultiMesh;

/**
 * Gives an Object geometry.
 *
 * Registers one DrawList entry per drawable MultiMesh part and unregisters
 * them when it dies, so nothing ever asks it what to draw. Hiding it drops the
 * entries; showing it registers them again.
 */
export class RenderComponent : public Component {
public:
    /// @param multiMesh the geometry; a null one never registers.
    explicit RenderComponent(std::shared_ptr<MultiMesh> multiMesh)
        : multiMesh_(std::move(multiMesh)) {}

    [[nodiscard]] const std::shared_ptr<MultiMesh>& getMultiMesh() const {
        return multiMesh_;
    }

    void setVisible(bool visible);

    [[nodiscard]] bool isVisible() const { return isVisible_; }

protected:
    /// Takes the Scene's DrawList and adds the entries that make this exist.
    void onAddedToScene() override;

    void onWorldTransformChanged() override;

private:
    /// One DrawList entry and the part transform its world matrix composes with.
    struct RegisteredPart {
        DrawHandle handle;
        glm::mat4  localTransform{1.0f};
    };

    /// Adds an entry for every part whose Mesh has geometry on the GPU.
    void registerParts();

    std::shared_ptr<MultiMesh>  multiMesh_;
    std::vector<RegisteredPart> parts_;
    bool                        isVisible_ = true;
};
