module;
#include <cstddef>
#include <vector>

export module VkScene:Subscribers;

/**
 * Everything subscribed to one event, and how that event reaches them.
 *
 * @tparam Target what receives the event; the list is homogeneous in it.
 * @tparam Signature the call the event makes, as `R(Args...)`.
 */
export template <typename Target, typename Signature>
class Subscribers;

template <typename Target, typename R, typename... Args>
class Subscribers<Target, R(Args...)> {
public:
    using Invoke = R (*)(Target&, Args...);

    constexpr void subscribe(Target& target, Invoke invoke) {
        entries_.push_back({&target, invoke});
    }


    constexpr void broadcast(Args... args) const {
        const std::size_t subscribed = entries_.size();
        for (std::size_t i = 0; i < subscribed; ++i)
            entries_[i].invoke(*entries_[i].target, args...);
    }

    /**
     * Transfers ownership of subscriptions
     */
    constexpr void take(Subscribers& from) {
        entries_.insert(entries_.end(), from.entries_.begin(), from.entries_.end());
        from.entries_.clear();
    }

    /**
     * Drops every subscription.
     */
    constexpr void clear() { entries_.clear(); }

    [[nodiscard]] constexpr std::size_t count() const { return entries_.size(); }
    [[nodiscard]] constexpr bool isEmpty() const { return entries_.empty(); }

private:
    struct Entry {
        Target* target;
        Invoke  invoke;
    };

    std::vector<Entry> entries_;
};
