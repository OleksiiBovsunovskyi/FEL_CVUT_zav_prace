module;
#include <memory>
#include <utility>

module VkScene;

void Scene::clearObjects() {
    /* Before the Objects die: the subscriptions are raw pointers into them,
     * and one left behind is a call into a destroyed object. One call covers
     * every event, so a new one cannot be forgotten here. */
    subscriptions_.clear();

    /* Components unregister through their handles as the Objects die, so the
     * DrawList empties itself. */
    objects_.clear();
}

/**
 * Broadcast tick event to everyone subscribed
 * @param deltaSeconds time since last tick event
 */
void Scene::tick(float deltaSeconds) {
    subscriptions_.broadcast<TickEvent>(deltaSeconds);
}
