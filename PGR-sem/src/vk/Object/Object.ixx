module;
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module VkScene:Object;

export import :Component;

export class Scene;

/**
 * Something placed in the world.
 *
 * Owns its transform and its components, and knows nothing about what any of
 * them do or what they registered themselves with.
 */
export class Object {
public:
    Object() = default;
    explicit Object(const glm::mat4& transform) : transform_(transform) {}

    /// Components are uniquely owned.
    Object(const Object&)            = delete;
    Object& operator=(const Object&) = delete;

    /* A moved Object leaves its components where they are, so each one has to
     * be told where its owner went. This is what lets a Scene keep Objects in
     * a vector. */
    Object(Object&& other) noexcept
        : transform_(other.transform_),
          components_(std::move(other.components_)),
          scene_(other.scene_) {
        repointComponents();
    }
    Object& operator=(Object&& other) noexcept {
        if (this != &other) {
            transform_  = other.transform_;
            components_ = std::move(other.components_);
            scene_      = other.scene_;
            repointComponents();
        }
        return *this;
    }

    ~Object() = default;

    /// Model-to-world.
    [[nodiscard]] const glm::mat4& getTransform() const { return transform_; }

    /// Tells every component, so anything derived from the transform follows.
    void setTransform(const glm::mat4& transform) {
        transform_ = transform;
        for (auto& component : components_) component->onOwnerTransformChanged();
    }

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

        /* The component was built before it had an owner, so anything it
         * derives from the transform is one call behind. Through the base,
         * which is what Object is a friend of. */
        components_.back()->onOwnerTransformChanged();
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
     *
     * Whatever removes an Object from a Scene must null this on the way out:
     * a stale pointer is indistinguishable from a live one.
     */
    [[nodiscard]] Scene* getScene() const { return scene_; }

private:
    friend class Scene;

    /// Called by Scene::addObject, once the Object is at its final address.
    void onAddedToScene(Scene& scene) {
        scene_ = &scene;
        for (auto& component : components_) component->onAddedToScene();
    }

    void repointComponents() {
        for (auto& component : components_) component->owner_ = this;
    }

    glm::mat4                               transform_{1.0f};
    std::vector<std::unique_ptr<Component>> components_;
    Scene*                                  scene_ = nullptr;
};
