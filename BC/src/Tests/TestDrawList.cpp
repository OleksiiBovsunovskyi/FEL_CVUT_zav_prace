/* Runtime checks of the DrawList slot map and change log. */

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import vulkan;
import DrawList;

namespace {

/// @return an instance whose transform's first column names it.
GPUMeshInstance instanceNamed(float id) {
    GPUMeshInstance instance{};
    instance.transform[0][0] = id;
    return instance;
}

DrawHandle addNamed(DrawList& list, float id) {
    return list.add(instanceNamed(id), glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
}

std::vector<uint32_t> taken(DrawList& list) {
    const std::span<const uint32_t> changed = list.takeChangedIndices();
    return std::vector<uint32_t>{changed.begin(), changed.end()};
}

/// Three adds log three indices, and taking twice leaves the log empty.
bool logsEveryAddOnce() {
    DrawList   list;
    DrawHandle first  = addNamed(list, 0.0f);
    DrawHandle second = addNamed(list, 1.0f);
    DrawHandle third  = addNamed(list, 2.0f);

    return taken(list) == std::vector<uint32_t>{0, 1, 2} && taken(list).empty() &&
           list.getItemsCount() == 3;
}

/// Writing one entry logs that entry's index and nothing else.
bool logsOnlyTheWrittenEntry() {
    DrawList   list;
    DrawHandle first  = addNamed(list, 0.0f);
    DrawHandle second = addNamed(list, 1.0f);
    DrawHandle third  = addNamed(list, 2.0f);
    (void)taken(list);

    second.setTransform(glm::mat4{7.0f});

    return taken(list) == std::vector<uint32_t>{1} &&
           list.getItems()[1].transform[0][0] == 7.0f;
}

/// Removal logs the index the tail was swapped into, and the handle follows it.
bool logsTheSwappedInEntry() {
    DrawList   list;
    DrawHandle first  = addNamed(list, 0.0f);
    DrawHandle second = addNamed(list, 1.0f);
    DrawHandle third  = addNamed(list, 2.0f);
    (void)taken(list);

    first = DrawHandle{};

    if (taken(list) != std::vector<uint32_t>{0}) return false;
    if (list.getItemsCount() != 2) return false;
    /* The tail moved into index 0, so the third handle now writes there. */
    if (list.getItems()[0].transform[0][0] != 2.0f) return false;

    third.setTransform(glm::mat4{9.0f});

    return taken(list) == std::vector<uint32_t>{0} &&
           list.getItems()[0].transform[0][0] == 9.0f;
}

/// Removing the last entry moves nothing, so it logs nothing.
bool logsNothingForATailRemoval() {
    DrawList   list;
    DrawHandle first  = addNamed(list, 0.0f);
    DrawHandle second = addNamed(list, 1.0f);
    (void)taken(list);

    second = DrawHandle{};

    return taken(list).empty() && list.getItemsCount() == 1;
}

[[maybe_unused]] const bool drawListChecked = [] {
    const bool passed = logsEveryAddOnce() && logsOnlyTheWrittenEntry() &&
                        logsTheSwappedInEntry() && logsNothingForATailRemoval();
    assert(passed && "the change log must name exactly the indices whose "
                     "records differ from what the GPU last received");
    return passed;
}();

} // namespace
