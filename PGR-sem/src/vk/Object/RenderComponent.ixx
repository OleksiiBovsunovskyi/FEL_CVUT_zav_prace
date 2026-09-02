module;
#include <memory>
#include <utility>

#include <glm/glm.hpp>

export module RenderComponent;

export import VkScene;
export import DrawList;

/**
 * Gives an Object geometry.
 *
 * Registers itself with the DrawList it is built against and unregisters when
 * it dies, so nothing ever asks it what to draw. Visibility is a flag on the
 * entry, not a re-registration.
 */
export class RenderComponent : public Component {
public:
    /// @param multiMesh the geometry; a null one never registers.
    explicit RenderComponent(std::shared_ptr<MultiMesh> multiMesh)
        : multiMesh_(std::move(multiMesh)) {}



    [[nodiscard]] const std::shared_ptr<MultiMesh>& getMultiMesh() const {
        return multiMesh_;
    }
 
    /// Hides the geometry without unregistering it.
    void setVisible(bool visible) {
        isVisible_ = visible;
        handle_.setVisible(visible);
    }

    [[nodiscard]] bool isVisible() const { return isVisible_; }

protected:
    /// Takes the Scene's DrawList and adds the entry that makes this exist.
    void onAddedToScene() override;

    void onWorldTransformChanged() override;

private:
    std::shared_ptr<MultiMesh> multiMesh_;
    DrawHandle                 handle_;
    bool                       isVisible_ = true;
};
