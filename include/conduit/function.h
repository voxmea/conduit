
// Copyright 2018 Qualcomm Technologies, Inc. All rights reserved.

#ifndef CONDUIT_FUNCTION_H_
#define CONDUIT_FUNCTION_H_

#include <memory>
#include <cstddef>
#include <cstdlib>

namespace conduit
{

template <typename, size_t BUF_SIZE = 16> class Function;
template <typename R, typename ...A, size_t BUF_SIZE>
class Function<R(A...), BUF_SIZE>
{
    static_assert(BUF_SIZE >= sizeof(void *), "BUF_SIZE must be at least size of a pointer");
    R (*call)(void *, A ...) = nullptr;
    void (*destruct)(void *) = nullptr;
    void (*clone)(Function *, const Function *) = nullptr;
    void *buf = nullptr;
    typename std::aligned_storage<BUF_SIZE, alignof(std::max_align_t)>::type internal_storage;

    template <typename T>
    void setup()
    {
        call = [] (void *buf, A ...a) -> R {return (*(T *)buf)(std::forward<A>(a)...);};
        destruct = [] (void *buf) {(*(T *)buf).~T();};
        clone = [] (Function *dst, const Function *src) {
            if (src->buf == nullptr) {
                dst->buf = nullptr;
                return;
            }
            if (sizeof(T) <= BUF_SIZE) {
                dst->buf = &dst->internal_storage;
            } else {
                dst->buf = malloc(sizeof(T));
            }
            new (dst->buf) T(*(T *)src->buf);
        };
    }

public:

    Function() = default;
    Function(const Function &o)
    {
        *this = o;
    }
    Function(Function &&o)
    {
        *this = std::move(o);
    }
    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Function>::value>::type>
    Function(T &&t)
    {
        using value_t = typename std::decay<T>::type;
        auto size = sizeof(value_t);
        if (size <= BUF_SIZE) {
            buf = &internal_storage;
        } else {
            buf = malloc(size);
        }
        new (buf) value_t(std::forward<T>(t));
        setup<value_t>();
    }

    ~Function()
    {
        if (buf != nullptr) {
            destruct(buf);
            if (buf != &internal_storage)
                free(buf);
            buf = nullptr;
        }
    }

    Function &operator =(std::nullptr_t *)
    {
        this->~Function();
    }

    Function &operator =(const Function &o)
    {
        if (this == &o)
            return *this;
        this->~Function();

        call = o.call;
        destruct = o.destruct;
        clone = o.clone;

        o.clone(this, &o);
        return *this;
    }

    Function &operator =(Function &&o)
    {
        if (this == &o)
            return *this;

        this->~Function();

        call = o.call;
        destruct = o.destruct;
        clone = o.clone;

        if (o.buf == &o.internal_storage) {
            o.clone(this, &o);
        } else {
            buf = o.buf;
        }
        o.buf = nullptr;
        return *this;
    }

    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Function>::value>::type>
    Function &operator =(T &&t)
    {
        this->~Function();

        using value_t = typename std::decay<T>::type;
        auto size = sizeof(value_t);
        if (size <= BUF_SIZE) {
            buf = &internal_storage;
        } else {
            buf = malloc(size);
        }
        new (buf) value_t(std::forward<T>(t));
        setup<value_t>();
        return *this;
    }

#if 1
    template <typename ...T>
    R operator()(T &&...t)
    {
        return call(buf, std::forward<T>(t)...);
    }

    template <typename ...T>
    R operator()(T &&...t) const
    {
        return call(buf, std::forward<T>(t)...);
    }
#else
    R operator()(A ...a) noexcept(noexcept(call(buf, std::forward<A>(a)...)))
    {
        return call(buf, std::forward<A>(a)...);
    }

    R operator()(A ...a) const noexcept(noexcept(call(buf, std::forward<A>(a)...)))
    {
        return call(buf, std::forward<A>(a)...);
    }
#endif

    explicit operator bool() const
    {
        return buf != nullptr;
    }

    // experimental JIT interface.
    std::tuple<void *, R(*)(void *, A...)> parts() const
    {
        return std::make_tuple(const_cast<void *>(buf), call);
    }
};

struct ScopeGuard
{
    template <typename T>
    ScopeGuard(T &&t) : f(std::forward<T>(t)) {} 
    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator =(const ScopeGuard &) = delete;

    ~ScopeGuard()
    {
        f();
    }

private:
    Function<void()> f;
};

}

#endif
