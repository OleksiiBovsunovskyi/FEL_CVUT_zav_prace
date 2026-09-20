module;
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

export module VkScene:Event;

export import :EventCapable;
export import :Subscribers;

/**
 * Every event there is, and how each one is detected and delivered.
 *
 * Adding one means adding its concept, its descriptor and its entry in
 * AllEventSubscriptions below, then calling broadcast where it fires. 
 */

/**
 * Every key and axis handled by input event
 */
export enum class Key {
    W,
    A,
    S,
    D,
    Q,
    E,
    /// Cursor delta along X, in pixels, positive to the right.
    MouseX,
    /// Cursor delta along Y, in pixels, positive downwards.
    MouseY,
};

/**
 * One input transition.
 *
 * A digital key carries 1 on press and 0 on release; 
 * a mouse axis carries its delta 
 */
export struct InputEventData {
    Key   key;
    float value;
};

/**
 * Handler should be public, otherwise invisible to the caller
 *
 * Handler should be declared AND EventCapable should be inherited
 */
export template <typename T>
concept DeclaresOnTick = requires(T& t, float deltaSeconds) {
    t.onTick(deltaSeconds);
};


struct OnTickProbe { void onTick(); };

template <typename T>
struct MergedWithOnTickProbe : T, OnTickProbe {};

/**
 * Whether a type declares onTick at all, public or not. False for anything
 * that cannot be derived from.
 */
export template <typename T>
concept DeclaresSomeOnTick =
    std::is_class_v<T> && !std::is_final_v<T> &&
    !requires { &MergedWithOnTickProbe<T>::onTick; };

/**
 * Event for objects and components to execute their per frame logic
 */
export struct TickEvent {
    using Signature = void(float);

    template <typename T>
    static constexpr bool declaresHandler = DeclaresOnTick<T>;

    /// Declared, but where a subscription cannot reach it.
    template <typename T>
    static constexpr bool declaresHiddenHandler =
        DeclaresSomeOnTick<T> && !DeclaresOnTick<T>;

    template <typename T>
    static constexpr void invoke(EventCapable& target, float deltaSeconds) {
        static_cast<T&>(target).onTick(deltaSeconds);
    }
};

template <typename Event>
struct ListFor : Subscribers<EventCapable, typename Event::Signature> {};

/**
 * One subscriber list per event, so a caller can subscribe something to
 * whatever it handles without naming an event. 
 */
export template <typename... Events>
class EventSubscriptions {
public:
    /// Adds target to the list of every event its type declares a handler for.
    template <typename T>
    constexpr void subscribe(T& target) {
        (subscribeTo<Events>(target), ...);
    }

    /// @tparam Event the one being fired;
    template <typename Event, typename... Args>
    constexpr void broadcast(Args&&... args) const {
        listFor<Event>().broadcast(std::forward<Args>(args)...);
    }

    /// Appends every list of `from` to this one's and empties `from`.
    constexpr void take(EventSubscriptions& from) {
        (listFor<Events>().take(from.template listFor<Events>()), ...);
    }

    /// Drops every subscription, of every event.
    constexpr void clear() { (listFor<Events>().clear(), ...); }

    template <typename Event>
    [[nodiscard]] constexpr std::size_t count() const { return listFor<Event>().count(); }

private:
    template <typename Event>
    [[nodiscard]] constexpr ListFor<Event>& listFor() {
        return std::get<ListFor<Event>>(lists_);
    }

    template <typename Event>
    [[nodiscard]] constexpr const ListFor<Event>& listFor() const {
        return std::get<ListFor<Event>>(lists_);
    }

    template <typename Event, typename T>
    constexpr void subscribeTo(T& target) {
        static_assert(!Event::template declaresHiddenHandler<T>,
                      "an event handler has to be public: the subscription is "
                      "made from outside the type, so a private or protected "
                      "one is invisible here");

        if constexpr (Event::template declaresHandler<T>)
            listFor<Event>().subscribe(target, &Event::template invoke<T>);
    }

    std::tuple<ListFor<Events>...> lists_;
};

/**
 * Handler should be public, otherwise invisible to the caller
 *
 * Handler should be declared AND EventCapable should be inherited
 */
export template <typename T>
concept DeclaresOnInput = requires(T& t, const InputEventData& event) {
    t.onInput(event);
};


struct OnInputProbe { void onInput(); };

template <typename T>
struct MergedWithOnInputProbe : T, OnInputProbe {};

/**
 * Whether a type declares onInput at all, public or not. False for anything
 * that cannot be derived from.
 */
export template <typename T>
concept DeclaresSomeOnInput =
    std::is_class_v<T> && !std::is_final_v<T> &&
    !requires { &MergedWithOnInputProbe<T>::onInput; };

/**
 * Event carrying one key transition or one mouse-axis delta.
 */
export struct InputEvent {
    using Signature = void(const InputEventData&);

    template <typename T>
    static constexpr bool declaresHandler = DeclaresOnInput<T>;

    /// Declared, but where a subscription cannot reach it.
    template <typename T>
    static constexpr bool declaresHiddenHandler =
        DeclaresSomeOnInput<T> && !DeclaresOnInput<T>;

    template <typename T>
    static constexpr void invoke(EventCapable& target, const InputEventData& event) {
        static_cast<T&>(target).onInput(event);
    }
};

/// The events this renderer has.
export using AllEventSubscriptions = EventSubscriptions<TickEvent, InputEvent>;
