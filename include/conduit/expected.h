
#ifndef EXPECTED_H_
#define EXPECTED_H_

#include <algorithm>
#include <type_traits>
#include <cstdlib>
#include <cassert>
#include <utility>
#include <ostream>

#include "botch.h"
#include "conduit-utility.h"

#if defined(__GNUC__) && (__GNUC__ > 5)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
// ignoring strict-aliasing warnings based on this:
// https://gcc.gnu.org/ml/gcc/2017-05/msg00013.html
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

namespace conduit {

namespace detail {
    template <typename T>
    struct Unexpected
    {
        T t;
        template <typename ...U>
        explicit Unexpected(U &&...u) : t(std::forward<U>(u)...) {}
    };

    template <typename U> struct Destructor { static void destruct(void *buf) {static_cast<U *>(buf)->~U();} };
    template <typename U> struct Destructor<U *> { static void destruct(void *) {} };
    template <> struct Destructor<void> { static void destruct (void *) {} };

    template <typename T, typename E>
    struct Base
    {
    protected:
        enum {
            IS_E = 1,
            NEEDS_CHECK = 2,
            E_INIT_STATE = IS_E | NEEDS_CHECK
        };
        mutable uint8_t state = 0;

        bool needs_check() const {return state & NEEDS_CHECK;}
        void clear_check() const {state = IS_E;}
        bool is_e() const {return state & IS_E;}

        void destruct(void *buf)
        {
            if (!is_e()) {
                Destructor<T>::destruct(buf);
            } else {
                Destructor<E>::destruct(buf);
            }
        }

        Base() = default;
        Base(const Base &) = default;
        Base(detail::Unexpected<E>) : state(E_INIT_STATE) {}

        template <typename U>
        typename std::enable_if<detail::has_putto_operator<U>::value>::type print_error(const U &u) const
        {
            std::cerr << "ERROR: " << u << "\n";
        }

        template <typename U>
        typename std::enable_if<!detail::has_putto_operator<U>::value>::type print_error(const U &) const
        {
        }

        #ifdef _MSC_VER
        #pragma warning(push)
        #pragma warning(disable : 4127) // constexpr in conditional
        #endif
        template <typename U>
        bool is_holding()
        {
            if (std::is_same<typename std::decay<U>::type, T>::value && !is_e())
                return true;
            else if (std::is_same<typename std::decay<U>::type, E>::value && is_e())
                return true;
            return false;
        }
        #ifdef _MSC_VER
        #pragma warning(pop)
        #endif

        template <typename U>
        typename std::enable_if<detail::has_assignment_operator<typename std::decay<U>::type>::value>::type assign(void *buf, U &&u)
        {
            using U_ = typename std::decay<U>::type;
            if (is_holding<U>()) {
                *static_cast<U_ *>(buf) = std::forward<U>(u);
                return;
            }

            using U_ = typename std::decay<U>::type;
            destruct(buf);
            new (buf) U_(std::forward<U>(u));
        }

        template <typename U>
        typename std::enable_if<!detail::has_assignment_operator<typename std::decay<U>::type>::value>::type assign(void *buf, U &&u)
        {
            using U_ = typename std::decay<U>::type;
            destruct(buf);
            new (buf) U_(std::forward<U>(u));
        }

    public:
        explicit operator bool() const
        {
            return !this->is_e();
        }
    };
}

template <typename T, typename E>
class Expected : public detail::Base<T, E> {
    typename std::aligned_storage<std::max(sizeof(T), sizeof(E)), std::max(alignof(T), alignof(E))>::type buf;

public:
    template <typename U, typename... V,
              typename
              = typename std::enable_if<!std::is_same<typename std::decay<U>::type, detail::Unexpected<E>>::value
                                        && !std::is_same<typename std::decay<U>::type, Expected>::value>::type>
    Expected(U &&u, V &&... v)
    {
        new (&buf) T(std::forward<U>(u), std::forward<V>(v)...);
    }

    Expected(detail::Unexpected<E> e) : detail::Base<T, E>(e)
    {
        new (&buf) E(std::move(e.t));
    }

    Expected(const Expected &e) : detail::Base<T, E>(e)
    {
        if (e) {
            new (&buf) T(*e);
        } else {
            new (&buf) E(e.error());
        }
    }

    Expected(Expected &&e) : detail::Base<T, E>(e)
    {
        if (e) {
            new (&buf) T(std::move(*e));
        } else {
            new (&buf) E(std::move(e.error()));
        }
    }

    ~Expected()
    {
        if (this->needs_check()) {
            this->print_error(this->error());
            BOTCH(true, "Destructing Expected<E> without checking E");
        }
        this->destruct(&buf);
    }

    Expected &operator =(const Expected &e)
    {
        if (this->needs_check()) {
            this->print_error(this->error());
            BOTCH(true, "Assigning Expected<E> without checking E");
        }
        // e.state needs to be copied before we change it by reading it below.
        const auto orig_state = e.state;
        if (e) {
            this->assign(&buf, *e);
        } else {
            this->assign(&buf, e.error());
        }
        this->state = orig_state;
        return *this;
    }

    Expected &operator =(Expected &&e)
    {
        if (this->needs_check()) {
            this->print_error(this->error());
            BOTCH(true, "Assigning Expected<E> without checking E");
        }
        // e.state needs to be copied before we change it by reading it below.
        const auto orig_state = e.state;
        if (e) {
            this->assign(&buf, std::move(*e));
        } else {
            this->assign(&buf, std::move(e.error()));
        }
        this->state = orig_state;
        return *this;
    }

    E &error()
    {
        BOTCH(!this->is_e(), "accessing E when we hold T");
        this->clear_check();
        return *reinterpret_cast<E *>(&buf);
    }

    const E &error() const
    {
        BOTCH(!this->is_e(), "accessing E when we hold T");
        this->clear_check();
        return *reinterpret_cast<const E *>(&buf);
    }

    T &operator *()
    {
        if (this->is_e()) {
            this->print_error(this->error());
            BOTCH(true, "accessing Expected T, when in fact Expected E");
        }
        return *reinterpret_cast<T *>(&buf);
    }

    const T &operator *() const
    {
        if (this->is_e()) {
            this->print_error(this->error());
            BOTCH(true, "accessing Expected T, when in fact Expected E");
        }
        return *reinterpret_cast<const T *>(&buf);
    }

    T *operator->()
    {
        if (this->is_e()) {
            this->print_error(this->error());
            BOTCH(true, "accessing Expected T, when in fact Expected E");
        }
        return reinterpret_cast<T *>(&buf);
    }

    const T *operator->() const
    {
        if (this->is_e()) {
            this->print_error(this->error());
            BOTCH(true, "accessing Expected T, when in fact Expected E");
        }
        return reinterpret_cast<const T *>(&buf);
    }
};

template <typename E>
class Expected<void, E> : public detail::Base<void, E>
{
    typename std::aligned_storage<sizeof(E), alignof(E)>::type buf;

public:
    Expected()
    {
    }

    Expected(detail::Unexpected<E> e) : detail::Base<void, E>(e)
    {
        new (&buf) E(std::move(e.t));
    }

    Expected(const Expected &e) : detail::Base<void, E>(e)
    {
        if (!e) {
            new (&buf) E(e.error());
        }
    }

    Expected(Expected &&e) : detail::Base<void, E>(e)
    {
        if (!e) {
            new (&buf) E(std::move(e.error()));
        }
    }

    ~Expected()
    {
        if (this->needs_check()) {
            this->print_error(this->error());
            BOTCH(true, "Destructing Expected<E> without checking E");
        }
        this->destruct(&buf);
    }

    Expected &operator =(const Expected &e)
    {
        if (this->needs_check()) {
            this->print_error(this->error());
            BOTCH(true, "Assigning Expected<E> without checking E");
        }
        // e.state needs to be copied before we change it by reading it below.
        const auto orig_state = e.state;
        if (!e) {
            *reinterpret_cast<E *>(&buf) = e.error();
        }
        this->state = orig_state;
        return *this;
    }

    Expected &operator =(Expected &&e)
    {
        if (this->needs_check()) {
            this->print_error(this->error());
            BOTCH(true, "Assigning Expected<E> without checking E");
        }
        // e.state needs to be copied before we change it by reading it below.
        const auto orig_state = e.state;
        if (!e) {
            this->assign(&buf, std::move(e.error()));
        }
        this->state = orig_state;
        return *this;
    }

    E &error()
    {
        BOTCH(!this->is_e(), "accessing E when we hold success");
        this->clear_check();
        return *reinterpret_cast<E *>(&buf);
    }

    const E &error() const
    {
        BOTCH(!this->is_e(), "accessing E when we hold success");
        this->clear_check();
        return *reinterpret_cast<const E *>(&buf);
    }
};

template <typename T, typename ...U>
detail::Unexpected<T> make_unexpected(U &&...u)
{
    return detail::Unexpected<T>(std::forward<U>(u)...);
}

}

#if defined(__GNUC__) && (__GNUC__ > 5)
#pragma GCC diagnostic pop
#endif

#endif
