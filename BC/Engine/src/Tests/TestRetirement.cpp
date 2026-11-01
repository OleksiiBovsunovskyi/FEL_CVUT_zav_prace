/* Runtime checks of Releaser release order and RangeAllocator range reuse. */

#include <cassert>
#include <vector>

import RangeAllocator;
import Releaser;

namespace {

/// Release steps run only once their serial has completed, oldest first; releaseAll runs the rest.
bool releasesOnlyCompletedSerials() {
    std::vector<int> released;
    const auto record = [&released](int item) { return [&released, item] { released.push_back(item); }; };

    Releaser releaser;
    releaser.setLastSubmittedFrameSerial(0);
    releaser.retire(record(10));
    releaser.setLastSubmittedFrameSerial(1);
    releaser.retire(record(20));
    releaser.retire(record(21));
    releaser.setLastSubmittedFrameSerial(2);
    releaser.retire(record(30));

    releaser.releaseCompleted(0);
    if (!released.empty()) return false;
    releaser.releaseCompleted(1);
    if (released != std::vector<int>{10}) return false;
    releaser.releaseCompleted(2);
    if (released != std::vector<int>{10, 20, 21}) return false;
    releaser.releaseAll();
    return released == std::vector<int>{10, 20, 21, 30} && releaser.pending() == 0;
}

/// A freed run of five in a full block is handed out again to the next request of five.
bool reusesAFreedRun() {
    RangeAllocator slots;
    if (!slots.init(8)) return false;

    const RangeAllocation first  = slots.allocate(5);
    const RangeAllocation second = slots.allocate(3);
    if (!first || !second || slots.allocate(1)) return false;

    slots.free(first);
    const RangeAllocation again = slots.allocate(5);
    return again && again.offset == first.offset && slots.used() == 8;
}

[[maybe_unused]] const bool retirementChecked = [] {
    const bool passed = releasesOnlyCompletedSerials() && reusesAFreedRun();
    assert(passed && "retired ranges must come back only after their serial completes, "
                     "and freed ranges must be reusable");
    return passed;
}();

} // namespace
