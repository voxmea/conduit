
// Copyright 2018 Qualcomm Technologies, Inc. All rights reserved.

#ifndef ROANOKE_ACCORDION_H_
#define ROANOKE_ACCORDION_H_

#include <vector>
#include <cstdint>
#include <iterator>
#include <tuple>
#include "fixvec.h"

namespace conduit
{

namespace detail
{
    template <typename A, bool is_const>
    class Iterator : public std::iterator<std::forward_iterator_tag, typename A::value_type>
    {
        using A_t = typename std::conditional<is_const, const A, A>::type;
        A_t *a = nullptr;
        enum Section
        {
            INVALID = 0,
            DENSE = 1,
            SPARSE = 2
        } section = DENSE;
        size_t cycle_offset = 0;

        Iterator(A_t &a_, bool) : a(&a_), section(SPARSE), cycle_offset(a->sparse.size()) {}
        friend A;

    public:

        Iterator() = default;
        Iterator(A_t &a_) : a(&a_)
        {
            auto i = std::find_if(a->dense.begin(), a->dense.end(), [] (typename A::Cycle *dc) {
                return dc != nullptr;
            });
            if (i == a->dense.end()) {
                section = SPARSE;
                return;
            }
            cycle_offset = std::distance(a->dense.begin(), i);
        }
        Iterator(const Iterator &o) noexcept = default;

        Iterator &operator =(const Iterator &) noexcept = default;
        Iterator &operator =(Iterator &&) noexcept = default;

        using value_type = typename A::value_type;
        using const_value_type = const typename A::value_type;

        typename A::Cycle *get()
        {
            switch (section) {
            case DENSE:
                return a->dense[cycle_offset];
                break;
            default:
                return *(a->sparse.rbegin() + cycle_offset);
                break;
            }
        }

        const typename A::Cycle *get() const
        {
            switch (section) {
            case DENSE:
                return a->dense[cycle_offset];
                break;
            default:
                return *(a->sparse.rbegin() + cycle_offset);
                break;
            }
        }

        template <typename T = value_type>
        typename std::enable_if<!is_const, T>::type &operator *() {return get()->elem;}
        const_value_type &operator *() const {return get()->elem;}
        template <typename T = value_type>
        typename std::enable_if<!is_const, T>::type *operator ->() {return &get()->elem;}
        const_value_type *operator ->() const {return &get()->elem;}

        Iterator operator ++(int)
        {
            auto ret = *this;
            *this = this->operator ++();
            return ret;
        }

        Iterator &operator ++()
        {
            ++cycle_offset;
            if (section == SPARSE) {
                return *this;
            }

            auto &dc = a->dense;
            while ((cycle_offset < dc.size()) && (dc[cycle_offset] == nullptr)) {
                ++cycle_offset;
            }
            if (cycle_offset == a->dense.size()) {
                section = SPARSE;
                cycle_offset = 0;
            }
            return *this;
        }

        uint64_t now() const
        {
            return get()->time;
        }

        friend bool operator ==(const Iterator &lhs, const Iterator &rhs)
        {
            return std::tie(lhs.section, lhs.cycle_offset) == std::tie(rhs.section, rhs.cycle_offset);
        }
        friend bool operator !=(const Iterator &lhs, const Iterator &rhs)
        {
            return std::tie(lhs.section, lhs.cycle_offset) != std::tie(rhs.section, rhs.cycle_offset);
        }
        friend bool operator <(const Iterator &lhs, const Iterator &rhs)
        {
            return std::tie(lhs.section, lhs.cycle_offset) < std::tie(rhs.section, rhs.cycle_offset);
        }
    };
}

template <typename T, size_t DenseSize = 10>
class Accordion
{
    struct Cycle
    {
        T elem;
        uint64_t time;
    };
    friend bool operator <(const Cycle &lhs, const Cycle &rhs)
    {
        return lhs.cycle < rhs.cycle;
    }

    std::array<Cycle *, DenseSize> dense = {};
    std::vector<Cycle *> sparse;
    std::vector<Cycle *> pool;

    uint64_t now_ = 0;

    template <typename U>
    struct ClearDetection
    {
        template <typename V> static constexpr auto has_clear_(V &&v) -> decltype(std::forward<V>(v).clear(), uint64_t()) {return 0;}
        static constexpr uint8_t has_clear_(...) {return '\0';}
        enum {
            has_clear = sizeof(decltype(has_clear_(std::declval<U>()))) == sizeof(uint64_t)
        };
    };

    template <typename U = T>
    typename std::enable_if<ClearDetection<U>::has_clear, Cycle *>::type allocate_cycle(uint64_t time)
    {
        if (pool.size()) {
            auto ret = pool.back();
            pool.pop_back();
            ret->time = time;
            return ret;
        }
        // TODO: make exception safe (unique, get, release, etc.)
        auto ptr = (Cycle *)malloc(sizeof(Cycle));
        ptr->time = time;
        new (&ptr->elem) T();
        return ptr;
    }

    template <typename U = T>
    typename std::enable_if<!ClearDetection<U>::has_clear, Cycle *>::type allocate_cycle(uint64_t time)
    {
        if (pool.size()) {
            auto ret = pool.back();
            pool.pop_back();
            ret->time = time;
            new (&ret->elem) T();
            return ret;
        }
        // TODO: make exception safe (unique, get, release, etc.)
        auto ptr = (Cycle *)malloc(sizeof(Cycle));
        ptr->time = time;
        new (&ptr->elem) T();
        return ptr;
    }

    template <typename U = T>
    typename std::enable_if<ClearDetection<U>::has_clear>::type deallocate_cycle(Cycle *&cycle)
    {
        cycle->time = std::numeric_limits<uint64_t>::max();
        cycle->elem.clear();
        pool.push_back(cycle);
        cycle = nullptr;
    }

    template <typename U = T>
    typename std::enable_if<!ClearDetection<U>::has_clear>::type deallocate_cycle(Cycle *&cycle)
    {
        cycle->time = std::numeric_limits<uint64_t>::max();
        cycle->elem.~T();
        pool.push_back(cycle);
        cycle = nullptr;
    }

    friend detail::Iterator<Accordion, true>;
    friend detail::Iterator<Accordion, false>;

    void compress()
    {
        auto di = std::find_if(dense.begin(), dense.end(), [] (Cycle *c) {
            return c != nullptr;
        });

        auto start_from_sparse = [&] {
            if (sparse.empty())
                return dense.end();
            di = dense.begin();
            *di = sparse.back();
            now_ = (*di)->time;
            sparse.pop_back();
            return di;
        };

        if (di == dense.end()) {
            di = start_from_sparse();
            if (di == dense.end())
                return;
            ++di;
        } else {
            now_ += std::distance(dense.begin(), di);
            di = std::rotate(dense.begin(), di, dense.end());
        }

        // di represents where we'll start trying to fill from sparse
        for (; (di != dense.end()) && (!sparse.empty()); ++di) {
            auto time_to_fill = now_ + std::distance(dense.begin(), di);
            if (time_to_fill == sparse.back()->time) {
                *di = sparse.back();
                sparse.pop_back();
            }
        }
    }

public:

    using value_type = T;
    using iterator = detail::Iterator<Accordion, false>;
    using const_iterator = detail::Iterator<Accordion, true>;

    Accordion() = default;
    Accordion(const Accordion &) = delete;
    Accordion &operator =(const Accordion &) = delete;

    ~Accordion()
    {
        clear();
        std::for_each(pool.begin(), pool.end(), [] (Cycle *c) {
            if (ClearDetection<T>::has_clear)
                c->elem.~T();
            free(c);
        });
        pool.clear();
    }

    T &operator [](size_t time)
    {
        if (time < now_)
            ::abort();
        auto offset = time - now_;
        if (offset < dense.size()) {
            auto &cycle = dense[offset];
            if (cycle == nullptr)
                cycle = allocate_cycle(time);
            return cycle->elem;
        }
        auto riter = std::lower_bound(sparse.rbegin(), sparse.rend(), time, [] (Cycle *c, uint64_t time) {
            return c->time < time;
        });
        if ((riter == sparse.rend()) || ((*riter)->time != time)) {
            auto iter = sparse.insert(riter.base(), allocate_cycle(time));
            return (*iter)->elem;
        }
        return (*riter)->elem;
    }

    // TODO: add insert/emplace

    std::tuple<uint64_t, T &> front()
    {
        auto &c = dense.front();
        if (dense.front() != nullptr) {
            return std::tuple<uint64_t, T &>(c->time, c->elem);
        }
        compress();
        return std::tuple<uint64_t, T &>(c->time, c->elem);
    }

    std::tuple<uint64_t, const T &> front() const
    {
        auto &c = dense.front();
        if (dense.front() != nullptr) {
            return std::tuple<uint64_t, const T &>(c->time, c->elem);
        }
        compress();
        return std::tuple<uint64_t, const T &>(c->time, c->elem);
    }

    void pop_front()
    {
        auto di = std::find_if(dense.begin(), dense.end(), [] (Cycle *c) {
            return c != nullptr;
        });

        auto start_from_sparse = [&] {
            if (sparse.empty())
                return dense.end();
            di = dense.begin();
            *di = sparse.back();
            now_ = (*di)->time;
            sparse.pop_back();
            return di;
        };

        if (di == dense.end()) {
            di = start_from_sparse();
            if (di == dense.end())
                ::abort();
        }
        deallocate_cycle(*di);
        ++di;
        if (di == dense.end()) {
            di = start_from_sparse();
            if (di == dense.end())
                return;
            ++di;
        } else {
            now_ += std::distance(dense.begin(), di);
            di = std::rotate(dense.begin(), di, dense.end());
            const bool dense_empty = std::all_of(dense.begin(), di, [] (Cycle *c) {
                return c == nullptr;
            });
            if (dense_empty)
                di = start_from_sparse();
        }
        // di == dense.end() means empty
        if (di == dense.end()) {
            if (!sparse.empty())
                ::abort();
            return;
        }

        // di represents where we'll start trying to fill from sparse
        for (; (di != dense.end()) && (!sparse.empty()); ++di) {
            auto time_to_fill = now_ + std::distance(dense.begin(), di);
            if (time_to_fill == sparse.back()->time) {
                *di = sparse.back();
                sparse.pop_back();
            }
        }
        di = std::find_if(dense.begin(), dense.end(), [] (Cycle *c) {
            return c != nullptr;
        });
        if (di == dense.end())
            ::abort();
    }

    uint64_t now() const
    {
        return now_;
    }

    bool empty() const
    {
        return std::all_of(dense.begin(), dense.end(), [] (Cycle *c) {return c == nullptr;}) && sparse.empty();
    }

    void clear()
    {
        std::for_each(dense.begin(), dense.end(), [=] (Cycle *&c) {
            if (c != nullptr) {
                deallocate_cycle(c);
            }
        });
        std::for_each(sparse.begin(), sparse.end(), [=] (Cycle *c) {
            deallocate_cycle(c);
        });
        sparse.clear();
    }

    void reset()
    {
        clear();
        now_ = 0;
    }

    iterator begin() {return iterator{*this};}
    const_iterator begin() const {return const_iterator{*this};}
    const_iterator cbegin() const {return const_iterator{*this};}
    iterator end() {return iterator{*this, true};}
    const_iterator end() const {return const_iterator{*this, true};}
    const_iterator cend() const {return const_iterator{*this, true};}
};

}

#endif
