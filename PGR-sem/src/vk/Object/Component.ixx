module;
/* A partition of VkScene: Component names Object and Scene, Object names
 * Component, and Scene names Object - one module has to own all three. */
export module VkScene:Component;

export class Object;

/**
 * A capability attached to one Object.
 *
 * Knows its owner and nothing about the Objects around it. Owned by the Object
 * through a pointer, so the Object may move without moving the component.
 */
export class Component {
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

protected:
    /**
     * The owner joined a Scene, reachable from here on through
     * getOwner().getScene(). A component that has to register itself somewhere
     * does it here; one that does not ignores this.
     */
    virtual void onAddedToScene() {}

    /// The owner's transform changed; anything derived from it is now stale.
    virtual void onOwnerTransformChanged() {}

private:
    friend class Object;

    Object* owner_ = nullptr;
};
