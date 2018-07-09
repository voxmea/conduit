
#ifndef CONDUIT_INCLUDE_CONDUIT_UTIL_H_
#define CONDUIT_INCLUDE_CONDUIT_UTIL_H_

#include <utility>
#include <functional>

// Stuff needed for demangle
#ifdef __GNUG__
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <memory>
#include <cxxabi.h>
#endif

namespace conduit {

namespace detail
{
    template<typename...> struct make_void { typedef void type;};
    template<typename... T> using void_t = typename make_void<T...>::type;

    template <typename, typename = void> struct has_putto_operator : std::false_type {};
    template <typename T> struct has_putto_operator<T, void_t<decltype(std::declval<std::ostream &>() << std::declval<T>())>> : std::true_type {};

    template <typename, typename = void> struct has_assignment_operator : std::false_type {};
    template <typename T> struct has_assignment_operator<T, void_t<decltype(std::declval<T &>() = std::declval<T>())>> : std::true_type {};
}

template <typename T>
struct no_arg_callable {
    // sizeof of void is illegal, except MSVC allows it. See specialization below.
    template <typename U>
    static char test(U *u, char (*i)[sizeof(decltype(std::declval<U>()()))] = 0);
    template <typename U>
    static long test(...);
    enum { value = (sizeof(test<T>(0)) == 1) };
};

template <typename T>
struct no_arg_callable<T()> {
    enum { value = 1 };
};

// necessary because MSVC is #@!$%@# busted.
template <>
struct no_arg_callable<std::function<void()>> {
    enum { value = 0 };
};

template <>
struct no_arg_callable<void()> {
    enum { value = 0 };
};

template <typename T, typename... V>
struct is_callable {
    template <typename U>
    static char test(U *u, decltype(std::declval<U>()(std::declval<V>()...)) *i = 0);
    template <typename U>
    static long test(...);
    enum { value = (sizeof(test<T>(0)) == 1) };
};

template <typename T, typename... U>
struct FirstType {
    typedef T type;
};

// Concatenate a series of tuples.
template <typename... T>
struct TupleCat;

template <typename... T>
struct TupleCat<std::tuple<T...>> {
    typedef std::tuple<T...> type;
};

template <typename... T, typename... U>
struct TupleCat<std::tuple<T...>, std::tuple<U...>> {
    typedef std::tuple<T..., U...> type;
};

template <typename... T, typename... U, typename... V>
struct TupleCat<std::tuple<T...>, std::tuple<U...>, V...> {
    typedef typename TupleCat<std::tuple<T..., U...>, V...>::type type;
};

#ifdef __GNUG__
inline std::string demangle(const char *name)
{
    int status = 0;
    auto s = abi::__cxa_demangle(name, nullptr, 0, &status);
    if (status != 0) {
        return "";
    }
    std::string ret{s};
    ::free(s);
    return ret;
}
#else
// does nothing if not g++
inline std::string demangle(const char *name)
{
    return name;
}
#endif
}
#endif
