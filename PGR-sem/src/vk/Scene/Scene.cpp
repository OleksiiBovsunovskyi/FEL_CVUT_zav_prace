module;
#include <memory>
#include <utility>

module VkScene;

Object& Scene::addObject(std::unique_ptr<Object> object) {
    Object& stored = *objects_.emplace_back(std::move(object));

    /* Entering the scene is what lets a component register itself; an Object
     * built and never added draws nothing. */
    stored.onAddedToScene(*this);
    return stored;
}

void Scene::clearObjects() {
    /* Components unregister through their handles as the Objects die, so the
     * DrawList empties itself. */
    objects_.clear();
}
