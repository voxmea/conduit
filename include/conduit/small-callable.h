
#ifndef COMMON_INCLUDE_ROANOKE_ANY_H_
#define COMMON_INCLUDE_ROANOKE_ANY_H_

#include "callable-info.h"
#include "conduit-utility.h"
#include "delayed-call.h"
#include "optional.h"
#include "binder.h"
#ifndef CONDUIT_NO_LUA
#include "lua-wrapper.h"
#endif

#include <type_traits>
#include <cstddef>
#include <cassert>
#include <typeindex>
#include <cstdint>
#include <iostream>

namespace conduit {


// no-heap std::function replacement
template <typename T, int BUF_SIZE = 256>
class SmallCallable;
template <typename T, typename... U, int BUF_SIZE>
class SmallCallable<T(U...), BUF_SIZE> {
    struct HolderBase {
        virtual ~HolderBase(){};
        virtual T operator()(const U &...) = 0;
        virtual void clone(char *) const = 0;
    };

    template <typename V_>
    struct VoidHolder final : public HolderBase {
        using V = std::decay_t<V_>;
        V v;
        VoidHolder(const V &v_) : v(v_) {}
        template <typename... U_>
        VoidHolder(U_ &&... u_) : v(std::forward<U_>(u_)...) {}
        VoidHolder(V &&v_) : v(std::move(v_)) {}
        ~VoidHolder() override {}
        T operator()(const U &... u) override
        {
            v(u...);
        }
        void clone(char *clone_buf) const override { new (clone_buf) VoidHolder<V>(v); }
    };

    template <typename V>
    struct VoidHolder<DelayedCall<V>> final : public HolderBase {
        DelayedCall<V> v;
        VoidHolder(const DelayedCall<V> &v_) : v(v_) {}
        template <typename U_, typename EI = typename std::enable_if<!std::is_same<std::decay_t<U_>, DelayedCall<V>>::value>::type, typename... W_>
        VoidHolder(U_ &&u_, W_ &&...w_) : v(std::forward<U_>(u_), std::forward<W_>(w_)...) {}
        VoidHolder(V &&v_) : v(std::move(v_)) {}
        ~VoidHolder() override {}
        T operator()() override { v(); }
        void clone(char *clone_buf) const override { new (clone_buf) VoidHolder<DelayedCall<V>>(v); }
    };

    template <typename V_>
    struct Holder final : public HolderBase {
        using V = std::decay_t<V_>;
        V v;
        Holder(const V &v_) : v(v_) {}
        template <typename U_, typename EI = typename std::enable_if<!std::is_same<std::decay_t<U_>, DelayedCall<V>>::value>::type, typename... W_>
        Holder(U_ &&u_, W_ &&...w_) : v(std::forward<U_>(u_), std::forward<W_>(w_)...) {}
        Holder(V &&v_) : v(std::move(v_)) {}
        ~Holder() final override {}
        T operator()(const U &... u) override
        {
            static_assert(std::is_convertible<typename CallableInfo<V>::return_type, T>::value, "v returns a type that can't be converted");
            return v(u...);
        }
        void clone(char *buf) const override { new (buf) Holder<V>(v); }
    };

    template <typename V, typename W>
    struct HolderType {
        typedef Holder<W> type;
    };
    template <typename W>
    struct HolderType<void, W> {
        typedef VoidHolder<W> type;
    };

    static const int buf_size = BUF_SIZE;
    typename std::aligned_storage<buf_size>::type buf;
    HolderBase *holder_base = reinterpret_cast<HolderBase *>(&buf);
    size_t mem_size;

public:
    SmallCallable() : mem_size(0) {}

    SmallCallable(const SmallCallable &o) : mem_size(o.mem_size)
    {
        if (o.mem_size)
            o.holder_base->clone(reinterpret_cast<char *>(holder_base));
    }

    SmallCallable(SmallCallable &&o) : mem_size(o.mem_size)
    {
        if (o.mem_size)
            o.holder_base->clone(reinterpret_cast<char *>(holder_base));
    }

    template <typename V>
    SmallCallable(V &&v) : mem_size(0)
    {
        *this = v;
    }

    void destroy()
    {
        if (mem_size) {
            holder_base->~HolderBase();
        }
    }

    ~SmallCallable()
    {
        destroy();
        mem_size = 0;
    }

    SmallCallable &operator=(const SmallCallable &o)
    {
        if (this == &o)
            return *this;
        destroy();
        o.holder_base->clone(reinterpret_cast<char *>(holder_base));
        mem_size = o.mem_size;
        return *this;
    }

    SmallCallable &operator=(SmallCallable &&o)
    {
        destroy();
        o.holder_base->clone(reinterpret_cast<char *>(holder_base));
        mem_size = o.mem_size;
        return *this;
    }

    template <typename V>
    typename std::enable_if<!std::is_same<std::decay_t<V>, SmallCallable>::value, SmallCallable>::type &operator=(V &&v)
    {
        using V_ = std::decay_t<V>;
        using H = typename HolderType<T, V_>::type;
        static_assert(sizeof(H) <= buf_size, "T exceeds SmallCallable capacity");
        static_assert(is_callable<H, U...>::value, "V cannot be called");
        destroy();
        mem_size = sizeof(H);
        new (holder_base) H(std::forward<V>(v));
        return *this;
    }

    template <typename... V>
    T operator()(V &&...v)
    {
        assert(mem_size > 0);
        return holder_base->operator()(std::forward<V>(v)...);
    }

    template <typename... V>
    T operator()(V &&...v) const
    {
        assert(mem_size > 0);
        return holder_base->operator()(std::forward<V>(v)...);
    }

    template <typename V, typename... W>
    void emplace(W &&... w)
    {
        using V_ = std::decay_t<V>;
        using H = typename HolderType<T, V_>::type;
        static_assert(sizeof(H) <= buf_size, "T exceeds SmallCallable capacity");
        static_assert(is_callable<H, U...>::value, "V cannot be called");
        destroy();
        mem_size = sizeof(H);
        new (holder_base) H(std::forward<W>(w)...);
    }

    explicit operator bool() const
    {
        return mem_size != 0;
    }
};
}

#endif
