
#ifndef OPTIONAL_H_
#define OPTIONAL_H_

#include <type_traits>
#include <cstdlib>
#include <cassert>

#include <conduit/botch.h>

// This is required because MSVC issues a warning about the variadic constructor
// even though SFINAE is working. Ugh.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4520)
#define alignof _alignof
#endif

#if defined(__GNUC__) && (__GNUC__ > 5)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
// ignoring strict-aliasing warnings based on this:
// https://gcc.gnu.org/ml/gcc/2017-05/msg00013.html
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

namespace conduit {
struct OptionalNull {
};

template <typename T>
struct Optional {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type buf;
    bool engaged_; 

    template <typename... V>
    struct FirstType {
        using type = Optional;
    };
    template <typename U, typename... V>
    struct FirstType<U, V...> {
        using type = U;
    };
    template <typename... V>
    struct is_not_optional {
        using FT_ = typename FirstType<V...>::type;
        using FT = typename std::decay<FT_>::type;
        enum { value = !std::is_same<FT, Optional>::value };
    };
    template <typename U, typename... V>
    struct constructor_enabler {
        enum { value = is_not_optional<U>::value || (sizeof...(V) > 0)};
    };

    Optional() : engaged_(false) {}

#if 1
    template <typename U, typename... V, typename E = typename std::enable_if<constructor_enabler<U, V...>::value>::type>
    Optional(U &&u, V &&... v) : engaged_(true)
    {
        new (&buf) T(std::forward<U>(u), std::forward<V>(v)...);
    }
#else
    Optional(const T &t_) : engaged_(true)
    {
        new (t) T(t_);
    }
    Optional(T &&t_) : engaged_(true) { new (t) T(std::move(t_)); }
#endif

    Optional(const OptionalNull &) : engaged_(false)
    {
    }
    Optional(OptionalNull &&) : engaged_(false) {}
    Optional(const Optional &o) : engaged_(o.engaged_)
    {
        if (engaged_) new (&buf) T(*o);
    }
    Optional(Optional &&o) : engaged_(o.engaged_)
    {
        if (engaged_) {
            new (&buf) T(std::move(*o));
        }
    }

    void destroy()
    {
        if (engaged_) {
            reinterpret_cast<T *>(&buf)->~T();
            engaged_ = false;
        }
    }

    ~Optional()
    {
        destroy();
    }

    Optional &operator=(const Optional &o)
    {
        if (this == &o)
            return *this;
        destroy();
        engaged_ = o.engaged_;
        if (engaged_)
            new (&buf) T(*o);
        return *this;
    }

    Optional &operator=(Optional &&o)
    {
        if (this == &o)
            return *this;
        destroy();
        engaged_ = o.engaged_;
        if (engaged_) {
            new (&buf) T(std::move(*o));
        }
        return *this;
    }

    Optional &operator=(OptionalNull)
    {
        destroy();
        return *this;
    }

    template <typename U>
    typename std::enable_if<is_not_optional<U, Optional>::value, Optional>::type &operator=(U &&u)
    {
        destroy();
        engaged_ = true;
        new (&buf) T(std::forward<U>(u));
        return *this;
    }

    explicit operator bool() const
    {
        return engaged_;
    }

    bool engaged() const
    {
        return engaged_;
    }

    T &operator*()
    {
        BOTCH(!engaged_, "accessing unset optional");
        return *reinterpret_cast<T *>(&buf);
    }

    const T &operator*() const
    {
        BOTCH(!engaged_, "accessing unset optional");
        return *reinterpret_cast<const T *>(&buf);
    }

    T *operator->()
    {
        BOTCH(!engaged_, "accessing unset optional");
        return reinterpret_cast<T *>(&buf);
    }

    const T *operator->() const
    {
        BOTCH(!engaged_, "accessing unset optional");
        return reinterpret_cast<const T *>(&buf);
    }
};

template <typename T>
std::ostream &operator<<(std::ostream &stream, const Optional<T> &op)
{
    if (!op) {
        stream << "{{empty}}";
    } else {
        stream << *op;
    }
    return stream;
}
}

#ifndef CONDUIT_NO_LUA
#include "lua-wrapper.h"
namespace conduit {
template <typename T> void push_arg(lua_State *L, const Optional<T> &t)
{
    if (!t) {
        lua_pushnil(L);
        return;
    }
    using conduit::push_arg;
    push_arg(L, *t);
}
}
#endif

#if __cplusplus > 201103L
// todo: this is illegal, but works
namespace std
{
template <typename T> struct is_trivially_copyable<conduit::Optional<T>> : is_trivially_copyable<T> {};
}
#endif

namespace conduit {
namespace detail
{
    struct TupleState
    {
        uint64_t val = 0;
    };

    template <typename ...T> struct Tuple;
    template <typename T, typename ...U>
    struct Tuple<T, U...>
    {
        using type = T;
        typename std::aligned_storage<sizeof(T), alignof(T)>::type buf;
        Tuple<U...> rest;
    };
    template <>
    struct Tuple<>
    {
        using type = int;
    };

    template <typename ...U> struct Tuple<void, U...> { using type = void; Tuple<U...> rest; };
    template <typename T> struct Tuple<T> { using type = T; typename std::aligned_storage<sizeof(T), alignof(T)>::type buf; };
    template <> struct Tuple<void> { using type = void; };

    template <int, typename> struct TupleElement;
    template <int index, typename T, typename ...U> struct TupleElement<index, Tuple<T, U...>> : TupleElement<index - 1, Tuple<U...>> {};
    template <typename T, typename ...U> struct TupleElement<0, Tuple<T, U...>>
    {
        using element_type = typename Tuple<T, U...>::type;
        using tuple_type = Tuple<T, U...>;
    };

    template <int index> struct TupleGet
    {
        template <typename ...T>
        static auto get(Tuple<T...> &t) -> decltype(TupleGet<index - 1>::get(t.rest))
        {
            return TupleGet<index - 1>::get(t.rest);
        }
    };

    template <> struct TupleGet<0>
    {
        template <typename ...T>
        static auto get(Tuple<T...> &t) -> decltype(t) &
        {
            return t;
        }
    };

    template <int arg> struct TupleGetVal
    {
        template <typename ...T>
        static auto get(Tuple<T...> &t) -> decltype(TupleGetVal<arg - 1>::get(t.rest))
        {
            return TupleGetVal<arg - 1>::get(t.rest);
        }
    };

    template <> struct TupleGetVal<0>
    {
        template <typename ...T>
        static auto get(Tuple<T...> &t) -> typename TupleElement<0, Tuple<T...>>::element_type &
        {
            using element_type = typename TupleElement<0, Tuple<T...>>::element_type;
            return *reinterpret_cast<element_type *>(&t.buf);
        }
    };

    template <int arg> struct TupleSet
    {
        template <typename V, typename ...T>
        static void set(Tuple<T...> &t, V &&v)
        {
            TupleSet<arg - 1>::set(t, std::forward<V>(v));
        }
    };

    template <> struct TupleSet<0>
    {
        template <typename V, typename ...T>
        static void set(TupleState &state, Tuple<T...> &t, V &&v)
        {
            using element_type = typename TupleElement<0, Tuple<T...>>::element_type;
            new (&t.buf) element_type(std::forward<V>(v));
        }
    };
}

}

#ifndef CONDUIT_NO_PYTHON
namespace pybind11 { namespace detail {

template <typename T> struct type_caster<conduit::Optional<T>>
{
    PYBIND11_TYPE_CASTER(conduit::Optional<T>, _("conduit::Optional<T>"));

    bool load(handle src, bool)
    {
        if (src.is_none()) {
            value = conduit::OptionalNull();
            return true;
        }
        value = src.cast<T>();
        return true;
    }

    static handle cast(const conduit::Optional<T> &o, return_value_policy, handle)
    {
        if (!o.engaged())
            return pybind11::none();
        auto ret = pybind11::cast(*o);
        ret.inc_ref();
        return ret;
    }
};

}} // namespace pybind11::detail
#endif


#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if defined(__GNUC__) && (__GNUC__ > 5)
#pragma GCC diagnostic pop
#endif

#endif
