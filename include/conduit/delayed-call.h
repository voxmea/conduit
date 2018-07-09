
#ifndef CONDUIT_INCLUDE_DELAYED_CALL_H_
#define CONDUIT_INCLUDE_DELAYED_CALL_H_

#include <conduit/callable-info.h>
#include <tuple>
#include <type_traits>
#include <utility>

namespace conduit {

template <typename C_>
class DelayedCall {
    using C = std::decay_t<C_>;
    using R = typename CallableInfo<C>::return_type;
    using TT = typename CallableInfo<C>::tuple_parameter_type;
    C callable;

    template <std::size_t ...I>
    R call_(std::true_type, std::index_sequence<I...>)
    {
        callable(std::get<I>(args)...);
    }

    template <std::size_t ...I>
    R call_(std::false_type, std::index_sequence<I...>)
    {
        return callable(std::get<I>(args)...);
    }

public:
    TT args;

    template <typename U, typename ...T, typename = typename std::enable_if<!std::is_same<typename std::decay<U>::type, DelayedCall>::value>::type>
    explicit DelayedCall(U &&callable_, T &&... t)
        : callable(std::forward<U>(callable_)), args(std::forward<T>(t)...)
    {
        static_assert(std::tuple_size<TT>::value == sizeof...(T), "Incorrect number of arguments to DelayedCall.");
    }
    DelayedCall(const DelayedCall &o) : callable(o.callable), args(o.args) {}
    DelayedCall(DelayedCall &&o) : callable(std::move(o.callable)), args(std::move(o.args)) {}

    R operator()()
    {
        return call_(typename std::is_same<R, void>::type(), typename CallableInfo<C>::seq_type());
    }
};

template <typename U, typename ...T>
DelayedCall<U> make_delayed(U &&callable, T &&...t)
{
    return DelayedCall<U>(std::forward<U>(callable), std::forward<T>(t)...);
}

}

#endif
