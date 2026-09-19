module;
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module VkScene:Object;

export import :Component;
export import :Transformable;
export import :Event;

export class Scene;

/**
 * Something placed in the world.
 *
 * Owns its transform and its components.
 */
export class Object : public Transformable, public EventCapable {
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
     * @return the stored component.
     */
    template <typename T>
    T& addComponent(T component) {
        auto owned    = std::make_unique<T>(std::move(component));
        T&   stored   = *owned;
        stored.owner_ = this;
        components_.push_back(std::move(owned));
        
        //Events component is subscribed to are defined by its declared methods.
        //For example public: void onTick(float dt);  
        pendingSubscriptions_.subscribe(stored);

        components_.back()->onWorldTransformChanged();

        /* Already in a Scene: the component is attached here, so nothing has
         * to walk the Objects looking for one that is not. */
        if (scene_) {
            components_.back()->onAddedToScene();
            flushSubscriptions();
        }

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
    /**
     * Called when object transform changed
     * Notifies all the components owned
     */
    void onTransformChanged() override {
        for (auto& component : components_) component->onWorldTransformChanged();
    }

private:
    friend class Scene;


    /**Called after object is added to the scene
     * @param scene - scene object was added to
     */
    void onAddedToScene(Scene& scene);

    /// Hands everything subscribed so far to the Scene, if there is one yet.
    void flushSubscriptions();

    std::vector<std::unique_ptr<Component>> components_;
    Scene*                                  scene_ = nullptr;

    /* Where subscriptions wait while this Object has no Scene to put them in.
     * Empty from the moment it joins one, since everything after that routes
     * straight through. */
    AllEventSubscriptions pendingSubscriptions_;
};
