module;
#include <glm/glm.hpp>

export module VkScene:Component;

export import :Transformable;

export class Object;

/**
 * A capability attached to one Object.
 *
 * Owned by the Object through a pointer
 */
export class Component : public Transformable {
public:
    Component() = default;
    virtual ~Component() = default;

    /// Owned by one Object; there is no second reference to copy.
    Component(const Component&)            = delete;
    Component& operator=(const Component&) = delete;

    /* Movable so a derived component can be built as a value and handed to
     * Object::addComponent; owner_ is set by the Object that takes it. */
    Component(Component&&) noexcept            = default;
    Component& operator=(Component&&) noexcept = default;

    /// Valid from Object::addComponent onwards, for the Object's life.
    [[nodiscard]] Object& getOwner() const { return *owner_; }

    /// Model-to-world: the owner's transform with this component's on top.
    [[nodiscard]] glm::mat4 getWorldTransform() const;

protected:
    /**
     * The owner joined a Scene, reachable from here on through
     * getOwner().getScene().
     */
    virtual void onAddedToScene() {}

    /// Either transform this composes from changed; the world one is stale.
    virtual void onWorldTransformChanged() {}

    /* final: a component overriding the local hook instead of the world one
     * would miss every move of its owner. */
    void onTransformChanged() final { onWorldTransformChanged(); }

private:
    friend class Object;

    Object* owner_ = nullptr;
};
