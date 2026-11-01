module;

#include <cassert>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <utility>

export module Releaser;

export import SubmissionSerial;
import Logger;

/**
 * Holds release steps until the GPU completes the submission they were retired under.
 * Release steps wait for the submission after the one set by Releaser::setLastSubmittedFrameSerial().
 * Release steps run in retirement order.
 */
export class Releaser {
public:
    using Release = std::function<void()>;

    Releaser() = default;

    /// Logs when release steps are still pending.
    ~Releaser();

    Releaser(const Releaser&)            = delete;
    Releaser& operator=(const Releaser&) = delete;

    /**
     * Serial of the last frame submitted.
     * Used to decide when resource is safe to free.
     * @param serial serial of the latest submitted frame.
     * @note Serials never decrease until Releaser::releaseAll().
     */
    void setLastSubmittedFrameSerial(SubmissionSerial serial) {
        assert(serial + 1 >= serial_ &&
               "Releaser::setLastSubmittedFrameSerial: serial went backwards");
        serial_ = serial + 1;
    }

    /// @param release step to run once the current serial completes.
    void retire(Release release) {
        assert(release && "Releaser::retire: empty release step");
        entries_.push_back(Entry{std::move(release), serial_});
    }

    /**
     * Runs every release step whose serial has completed, oldest first.
     * @param completed highest serial the GPU has completed.
     */
    void releaseCompleted(SubmissionSerial completed) {
        while (!entries_.empty() && entries_.front().serial <= completed) {
            const Release release = std::move(entries_.front().release);
            entries_.pop_front();
            release();
        }
    }

    /**
     * Runs every pending release step and resets the serial to zero.
     * @note The GPU must be idle before this is called.
     */
    void releaseAll() {
        while (!entries_.empty()) {
            const Release release = std::move(entries_.front().release);
            entries_.pop_front();
            release();
        }
        serial_ = 0;
    }

    /// @return release steps not yet run.
    [[nodiscard]] size_t pending() const { return entries_.size(); }

private:
    struct Entry {
        Release          release;
        SubmissionSerial serial = 0;
    };

    std::deque<Entry> entries_;
    SubmissionSerial  serial_ = 0;
};

Releaser::~Releaser() {
    if (!entries_.empty())
        logError("Releaser: destroyed with " + std::to_string(entries_.size()) +
                 " release steps pending; releaseAll() was not called");
}
