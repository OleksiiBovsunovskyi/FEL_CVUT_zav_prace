/* Compile-time checks of event subscription and delivery. */

// Flags to test compile time asserts
//#define TESTHIDDENONTICK //Should cause compilation failure. 
 
import VkScene;

namespace {
/**
 * Example of an object that should be able to tick (onTick declared + EventCapable).
 */
struct Ticks : EventCapable {
    bool ticked = false;
    constexpr void onTick(float) { ticked = true; }
};

/**
 * Example of an object that should **NOT** be able to tick (DOES NOT have onTick declared).
 */
struct DoesNotTick : EventCapable {};

/**
 * Example of an object that should **NOT** be able to tick (onTick is hidden).
 */
struct TicksHidden : EventCapable { protected: void onTick(float) {} };

/**
 * Example of an object that should be able to tick (Base covers all what needed to tick).
 */
struct TicksByBase : Ticks {};

/**
 * Example of an object that declares onTick but cannot be subscribed (NOT
 * EventCapable, so it does not fit the list).
 */
struct TicksWithoutEventCapable {
    void onTick(float) {}
};

static_assert(DeclaresOnTick<Ticks>);
static_assert(!DeclaresOnTick<DoesNotTick>);
static_assert(!DeclaresOnTick<TicksHidden>,
              "onTick has to be public: the subscription is made from outside "
              "component silently never ticks");
static_assert(DeclaresOnTick<TicksByBase>,
              "an inherited onTick still is one");
static_assert(DeclaresOnTick<TicksWithoutEventCapable>,
              "Has onTick declared, but missing EventCapable base class, "
              "this should not tick");


constexpr bool subscribesOnlyWhatHandles() {
    AllEventSubscriptions subscriptions;

    Ticks       ticks;
    DoesNotTick doesNotTick;
    TicksByBase ticksByBase;
    
    #ifdef TESTHIDDENONTICK //onTick is protected or private, this is not allowed
    {
        TicksHidden ticksHidden;   
        subscriptions.subscribe(ticksHidden);
    }
    #endif
    
    subscriptions.subscribe(ticks);
    subscriptions.subscribe(doesNotTick);
    subscriptions.subscribe(ticksByBase);
 
    return subscriptions.count<TickEvent>() == 2;
}

static_assert(subscribesOnlyWhatHandles(),
              "subscribe must add exactly the targets declaring onTick");

constexpr bool broadcastReachesTheHandler() {
    AllEventSubscriptions subscriptions;

    Ticks ticks;
    subscriptions.subscribe(ticks);
    subscriptions.broadcast<TickEvent>(1.0f);

    return ticks.ticked;
}

static_assert(broadcastReachesTheHandler(),
              "broadcast must reach onTick through the stored entry");

constexpr bool takeMovesEverySubscription() {
    AllEventSubscriptions pending;
    AllEventSubscriptions live;

    Ticks ticks;
    pending.subscribe(ticks);

    live.take(pending);

    return live.count<TickEvent>() == 1 && pending.count<TickEvent>() == 0;
}

static_assert(takeMovesEverySubscription(),
              "take must move the subscriptions and leave the source empty");

constexpr bool clearDropsEverySubscription() {
    AllEventSubscriptions subscriptions;

    Ticks ticks;
    subscriptions.subscribe(ticks);
    subscriptions.clear();

    return subscriptions.count<TickEvent>() == 0;
}

static_assert(clearDropsEverySubscription(),
              "clear must empty every list, or clearObjects leaves entries "
              "pointing at destroyed Objects");

} // namespace
