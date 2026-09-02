module;
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module VkScene:Object;

export import :Component;
export import :Transformable;

export class Scene;

/**
 * Something placed in the world.
 *
 * Owns its transform and its components, and knows nothing about what any of
 * them do or what they registered themselves with.
 */
export class Object : public Transformable {
public:
    Object() = default;
    explicit Object(const glm::mat4& transform) : Transformable(transform) {}

    /**Moving an object would invalidate pointers to that object**/
    Object(const Object&)            = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&)                 = delete;
    Object& operator=(Object&&)      = delete;

    /// Virtual: a Scene owns Objects through unique_ptr<Object>.
    virtual ~Object() = default;

    /**
     * Takes ownership of a constructed component.
     *
     * @return the stored component, whose address is stable for the Object's
     *         life.
     */
    template <typename T>
    T& addComponent(T component) {
        auto owned    = std::make_unique<T>(std::move(component));
        T&   stored   = *owned;
        stored.owner_ = this;
        components_.push_back(std::move(owned));

        components_.back()->onWorldTransformChanged();
        return stored;
    }

    /// @return the first component of that type, or null when there is none.
    template <typename T>
    [[nodiscard]] T* getComponent() const {
        for (const auto& component : components_)
            /* ponytail: linear scan over a handful of components; a type map
             * only if a profile ever says so. */
            if (T* match = dynamic_cast<T*>(component.get())) return match;
        return nullptr;
    }

    /**
     * The Scene this Object belongs to, or null before it joins one. A
     * component reaches the world through it.
     */
    [[nodiscard]] Scene* getScene() const { return scene_; }

protected:
    /// Every component composes from this, so every one of them is now stale.
    void onTransformChanged() override {
        for (auto& component : components_) component->onWorldTransformChanged();
    }

private:
    friend class Scene;

    /// Called by Scene::addObject, once the Object belongs to the Scene.
    void onAddedToScene(Scene& scene) {
        scene_ = &scene;
        for (auto& component : components_) component->onAddedToScene();
    }

    std::vector<std::unique_ptr<Component>> components_;
    Scene*                                  scene_ = nullptr;
};
