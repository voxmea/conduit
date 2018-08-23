
#ifndef EXAMPLES_EVENTS_H_
#define EXAMPLES_EVENTS_H_

#include <conduit/delayed-call.h>
#include <conduit/function.h>
#include <conduit/accordion.h>
#include <limits>
#include <tuple>
#include <initializer_list>
#include <functional>
#include <deque>
#include <vector>

namespace ev
{

struct Event
{
    uint64_t cycle;
    conduit::Function<void()> ev;

    template <typename T>
    Event(uint64_t c_, T &&t) : cycle(c_), ev(std::forward<T>(t)) {}

    friend bool operator <(const Event &l, const Event &r)
    {
        return l.cycle < r.cycle;
    }
    friend bool operator >(const Event &l, const Event &r)
    {
        return l.cycle > r.cycle;
    }
};

namespace detail
{
    using cycle = conduit::FixVec<Event, 1000>;
    using Scheduler = conduit::Accordion<cycle>;
    inline Scheduler &sched()
    {
        static Scheduler s;
        return s;
    }
}

inline uint64_t cycle()
{
    return detail::sched().now();
}

template <typename D>
void insert(uint64_t delta, D &&d)
{
    auto &s = detail::sched();
    Event ev{s.now() + delta, std::forward<D>(d)};
    s[s.now() + delta].emplace_back(std::move(ev));
}

template <typename T, typename... U>
void sched(uint64_t delta, T &&t, U &&... u)
{
    auto d = conduit::make_delayed(std::forward<T>(t), std::forward<U>(u)...);
    insert(delta, std::move(d));
}

namespace detail {
template <typename C> struct CompletionUnique {
    C c;
    bool scheduled;

    template <typename T>
    CompletionUnique(T &&t_)
        : c(std::forward<T>(t_)), scheduled(false) {}

    void operator()()
    {
        if (!scheduled) {
            scheduled = true;
            const auto &cb = [this] {
                scheduled = false;
                c();
            };
            ev::sched(0, cb);
        }
    }
};
}

template <typename C_>
std::string unique(C_ &&c,
                   std::initializer_list<conduit::ChannelInterface<void()>> il,
                   const std::string &hook_name = "")
{
    using C = std::decay_t<C_>;
    if (il.size() == 0)
        return hook_name;
    auto u = std::make_shared<detail::CompletionUnique<C>>(std::forward<C_>(c));
    for (auto &ci : il) {
        ci.hook([u] {u->operator ()(); }, hook_name);
    }
    return hook_name;
}

namespace detail {
    template <typename Callback, typename Filter, typename State>
    struct CompletionAccumulate {
        Callback callback;
        Filter filter;
        const State init_state;
        State state;
        bool scheduled;

        CompletionAccumulate(const Callback &c_, Filter f_, State is_)
            : callback(c_), filter(f_), init_state(is_), scheduled(false)
        {
        }

        void operator()(State new_state)
        {
            if (!scheduled) {
                scheduled = true;
                const auto &cb = [=] {
                    scheduled = false;
                    callback(state);
                };
                state = filter(init_state, new_state);
                ev::sched(0, cb);
            } else {
                state = filter(state, new_state);
            }
        }
    };
}

template <typename Callback, typename CallbackParam, typename Filter, typename State>
std::string unique(Callback c,
                   std::initializer_list<::conduit::ChannelInterface<void(CallbackParam)>> il,
                   int step,
                   Filter f,
                   State init,
                   std::string hook_name = "")
{
    if (il.size() == 0)
        ::abort();
    auto u = std::make_shared<detail::CompletionAccumulate<Callback, std::decay_t<Filter>, State>>(c, step, f, init);
    for (auto &ci : il) {
        ci.hook([u] (State s) {(*u)(s); });
    }
    return hook_name;
}

}

#endif
