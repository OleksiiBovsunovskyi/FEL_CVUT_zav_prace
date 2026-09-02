module;
#include <glm/glm.hpp>

export module VkScene:Transformable;

/**
 * Anything that has transforms
 *
 * Owns one transform and the notification that it changed;
 */
export class Transformable {
public:
    Transformable() = default;
    explicit Transformable(const glm::mat4& transform) : transform_(transform) {}

    /* Declared, so the implicit move operations are not suppressed by the
     * destructor below. */
    Transformable(const Transformable&)                = default;
    Transformable& operator=(const Transformable&)     = default;
    Transformable(Transformable&&) noexcept            = default;
    Transformable& operator=(Transformable&&) noexcept = default;

    [[nodiscard]] const glm::mat4& getTransform() const { return transform_; }

    void setTransform(const glm::mat4& transform) {
        transform_ = transform;
        onTransformChanged();
    }

protected:
    ~Transformable() = default;

    /// Anything derived from this transform is now stale.
    virtual void onTransformChanged() {}

private:
    glm::mat4 transform_{1.0f};
};
