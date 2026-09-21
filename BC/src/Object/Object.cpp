module;
#include <vector>

module VkScene;

void Object::flushSubscriptions() {
    if (scene_) scene_->subscriptions_.take(pendingSubscriptions_);
}

void Object::onAddedToScene(Scene& scene) {
    scene_ = &scene;

    for (auto& component : components_) component->onAddedToScene();

    flushSubscriptions();
}
