
// Copyright 2018 Qualcomm Technologies, Inc. All rights reserved.

#ifndef E2PM_INCLUDE_FIXVEC_
#define E2PM_INCLUDE_FIXVEC_

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#include <initializer_list>
#include <array>
#include <algorithm>
#include <iterator>
#include <utility>
#include <memory>
#include <cassert>

#ifdef USE_FIXVEC_POOL
#include <vector>
namespace mix {
namespace detail {

template <size_t N, size_t POOL_SIZE = 4096>
class pool
{
    std::vector<char *> queue;

public:
    pool() = default;
    pool(const pool &) = delete;
    pool &operator =(const pool &) = delete;

    ~pool()
    {
        for (auto p : queue)
            ::operator delete(p);
    }

    char *allocate()
    {
        if (queue.size()) {
            auto r = queue.back();
            queue.pop_back();
            return r;
        }
        return static_cast<char*>(::operator new(N));
    }

    void deallocate(char *p)
    {
        if (queue.size() < POOL_SIZE) {
            queue.push_back(p);
        } else {
            ::operator delete(p);
        }
    }

    static pool &get_pool()
    {
        static pool *p = new pool();
        return *p;
    }

    static char *get_pool_buffer()
    {
        return get_pool().allocate();
    }

    static void return_pool_buffer(char *p)
    {
        get_pool().deallocate(p);
    }
};
}
}
#endif

namespace mix {
template <typename T, size_t MAX, bool DEFAULT_CONSTRUCT = false>
class FixVec
{
    size_t num;
    #ifdef USE_FIXVEC_POOL
    T *data = (T *)detail::pool<sizeof(T) * MAX>::get_pool_buffer();
    #else
    T *const data = reinterpret_cast<T *>(&buf);
    typename std::aligned_storage<sizeof(T) * MAX, alignof(T)>::type buf;
    #endif

    template <bool u>
    typename std::enable_if<u>::type init()
    {
        if (DEFAULT_CONSTRUCT) {
            for (size_t i = 0; i < max_size; ++i) {
                new (data + i) T();
            }
            num = max_size;
        }
    }

    template <bool u>
    typename std::enable_if<!u>::type init()
    {
    }

public:
    static const size_t max_size = MAX;

    using size_type = size_t;
    using index_type = size_t;
    using value_type = T;
    using const_value_type = const T;
    using pointer = T *;
    using reference = T &;
    using iterator = T *;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_iterator = const T *;
    using const_reverse_iterator = const reverse_iterator;

private:

    // grow makes a gap, moving or copying elements as appropriate to higher
    // memory addresses
    void grow(iterator pos, size_t grow_len)
    {
        using std::move_backward;
        const size_t segment_len = std::distance(pos, end());
        {
            auto source = segment_len > grow_len ? end() - grow_len : pos;
            auto dest = end() + ((grow_len > segment_len) ? grow_len - segment_len : 0);
            for (auto i = (size_t)0; i < std::min(segment_len, grow_len); ++i) {
                new (dest++) T(std::move(*source++));
            }
        }
        if (segment_len > grow_len) {
            const auto remaining = segment_len - grow_len;
            move_backward(pos, pos + remaining, data + num);
        }
    }

public:

    FixVec() : num(0)
    {
        init<DEFAULT_CONSTRUCT>();
    }

    FixVec(size_t n) : num(n)
    {
        for (size_t i = 0; i < n; ++i) {
            new (data + i) T();
        }
    }

    FixVec(const T &t, size_t n) : num(n)
    {
        std::uninitialized_fill(data, data + num, t);
    }

    FixVec(const FixVec &fv) : num(fv.num)
    {
        std::uninitialized_copy(fv.begin(), fv.end(), data);
    }

    #ifdef USE_FIXVEC_POOL
    FixVec(FixVec &&fv) noexcept : num(fv.num), data(fv.data)
    {
        fv.num = 0;
        fv.data = nullptr;
    }
    #endif

    template <typename U, typename V = typename std::enable_if<!std::is_same<U, FixVec>::value>::type>
    FixVec(const std::initializer_list<U> &init) : num(init.size())
    {
        std::uninitialized_copy(init.begin(), init.end(), data);
    }

    template <typename Cont, typename V = decltype(std::begin(std::declval<Cont>()))>
    FixVec(const Cont &init) : num(init.size())
    {
        std::uninitialized_copy(init.begin(), init.end(), data);
    }

    template <typename U = T, typename std::enable_if<std::is_trivially_destructible<U>::value>::type * = nullptr>
    void destruct(U *t, size_t num)
    {
    }

    template <typename U = T, typename std::enable_if<!std::is_trivially_destructible<U>::value>::type * = nullptr>
    void destruct(U *t, size_t num)
    {
        for (size_t i = 0; i < num; ++i, ++t)
            t->~T();
    }

    ~FixVec()
    {
        #ifdef USE_FIXVEC_POOL
        if (data == nullptr)
            return;
        #endif
        destruct(data, num);
        num = 0;
        #ifdef USE_FIXVEC_POOL
        detail::pool<sizeof(T) * MAX>::return_pool_buffer((char *)data);
        data = nullptr;
        #endif
    }

    FixVec &assignment(const FixVec &other, std::true_type) noexcept
    {
        if (this == &other)
            return *this;

        memcpy(data, other.data, sizeof(T) * other.size());
        num = other.size();
        return *this;
    }

    FixVec &assignment(const FixVec &other, std::false_type)
    {
        if (this == &other)
            return *this;

        auto min = std::min(num, other.num);
        auto iter = std::copy(other.begin(), other.begin() + min, begin());
        const auto other_num = other.size();
        if (min < other_num)
            std::uninitialized_copy(other.begin() + min, other.end(), iter);
        if (other_num < num)
            destruct(data + other_num, num - other_num);
        num = other_num;
        return *this;
    }

    FixVec &assignment(const std::vector<T> &other, std::true_type) noexcept
    {
        const auto min = std::min(MAX, other.size());
        num = min;
        if (!other.empty())
            memcpy(data, &other[0], sizeof(T) * min);
        return *this;
    }

    FixVec &assignment(const std::vector<T> &other, std::false_type)
    {
        const auto other_num = other.size();
        const auto min = std::min(num, other_num);
        auto iter = std::copy(other.begin(), other.begin() + min, begin());
        if (min < other_num)
            std::uninitialized_copy(other.begin() + min, other.end(), iter);
        if (other_num < num)
            destruct(data + other_num, num - other_num);
        num = other_num;
        return *this;
    }

    FixVec &operator =(const FixVec &other) noexcept(noexcept(std::declval<FixVec>().assignment(other, typename std::is_trivially_copyable<T>::type())))
    {
        return assignment(other, typename std::is_trivially_copyable<T>::type());
    }

    FixVec &operator =(const std::vector<T> &other) noexcept(noexcept(std::declval<FixVec>().assignment(other, typename std::is_trivially_copyable<T>::type())))
    {
        return assignment(other, typename std::is_trivially_copyable<T>::type());
    }

    #ifdef USE_FIXVEC_POOL
    FixVec &operator =(FixVec &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (data) {
            destruct(data, num);
            detail::pool<sizeof(T) * MAX>::return_pool_buffer((char *)data);
        }
        this->data = other.data;
        this->num = other.num;
        other.num = 0;
        other.data = nullptr;
        return *this;
    }
    #endif

    FixVec &fill(const T &t, size_t new_num = MAX)
    {
        auto min = std::min(num, new_num);
        std::fill(data, data + min, t);
        if (min < new_num)
            std::uninitialized_fill(data + min, data + new_num, t);
        num = new_num;
        return *this;
    }

    iterator begin() { return data; }
    #if defined(MSVC) || (__cplusplus >= 201400L)
    reverse_iterator rbegin() { return std::make_reverse_iterator(end());}
    #endif
    const_iterator begin() const { return data; }
    const_iterator cbegin() const { return data; }
    iterator end() { return data + num; }
    #if defined(MSVC) || (__cplusplus >= 201400L)
    reverse_iterator rend() {return std::make_reverse_iterator(begin());}
    #endif
    const_iterator end() const { return data + num; }
    const_iterator cend() const { return data + num; }
    size_t size() const { return num; }
    size_t capacity() const { return MAX; }
    bool empty() const { return num == 0; }
    bool full() const { return num == MAX; }

    T &front() { return data[0]; }
    const T &front() const { return data[0]; }
    T &back() { return data[num - 1]; }
    const T &back() const { return data[num - 1]; }

    iterator erase(iterator pos)
    {
        using std::distance;
        using std::move;
        if (pos == end())
            return pos;
        const auto iter_dist = distance(begin(), pos);
        move(pos + 1, end(), pos);
        data[--num].~T();
        if (iter_dist >= static_cast<decltype(iter_dist)>(num)) {
            return end();
        }
        return begin() + iter_dist;
    }
    iterator erase(iterator range_begin, iterator range_end)
    {
        using std::distance;
        using std::move;
        const auto iter_dist = distance(begin(), range_begin);
        const auto range_dist = distance(range_begin, range_end);
        move(range_end, end(), range_begin);
        num -= range_dist;
        destruct(data + num, range_dist);
        if (iter_dist >= static_cast<decltype(iter_dist)>(num))
            return end();
        return begin() + iter_dist;
    }
    void clear()
    {
        erase(begin(), end());
    }

    void push_back(const T &val)
    {
        assert(num < MAX);
        new (data + num++) T(val);
    }
    void push_back(T &&val)
    {
        assert(num < MAX);
        new (data + num++) T(std::move(val));
    }
    void pop_back()
    {
        auto e = end();
        erase(--e);
    }

    template <typename... U>
    void emplace_back(U &&... u)
    {
        assert(num < MAX);
        new (data + num++) T(std::forward<U>(u)...);
    }

    iterator insert(iterator pos, T t)
    {
        assert(num < MAX);
        if (pos == end()) {
            push_back(std::move(t));
            return pos;
        }

        grow(pos, 1);
        *pos = std::move(t);
        ++num;
        return pos;
    }

    template <typename U>
    iterator insert(iterator pos, U range_begin, U range_end)
    {
        if (range_begin == range_end)
            return pos;

        if (pos == end()) {
            auto dist = size();
            while (range_begin != range_end)
                push_back(*range_begin++);
            return begin() + dist;
        }

        using std::distance;

        const auto ret_pos = pos;
        const auto fixvec_len = distance(pos, end());
        const auto insert_len = distance(range_begin, range_end);
        grow(pos, insert_len);
        for (int i = 0; i < insert_len; ++i) {
            if (i < fixvec_len) {
                *pos++ = *range_begin++;
            } else {
                new (pos++) T(*range_begin);
            }
        }
        num += insert_len;
        return ret_pos;
    }

    template <typename U>
    iterator insert(iterator pos, std::initializer_list<U> init)
    {
        return insert(pos, init.begin(), init.end());
    }

    template <typename... U>
    iterator emplace(iterator pos, U &&... u)
    {
        assert(num < MAX);
        if (pos == end()) {
            emplace_back(std::forward<U>(u)...);
            return pos;
        }

        grow(pos, 1);
        pos->~T();
        new (pos) T(std::forward<U>(u)...);
        ++num;
        return pos;
    }

    T &operator [](size_t index)
    {
        return data[index];
    }

    const T &operator [](size_t index) const
    {
        return data[index];
    }

    template <typename U>
    friend bool operator ==(const FixVec &lhs, const U &rhs)
    {
        if (lhs.size() != rhs.size())
            return false;
        return std::equal(lhs.begin(), lhs.end(), rhs.begin());
    }

    template <typename U>
    friend bool operator !=(const FixVec &lhs, const U &rhs)
    {
        return !(lhs == rhs);
    }

    template <typename U>
    friend bool operator <(const FixVec &lhs, const U &rhs)
    {
        if (lhs.empty() && !rhs.empty())
            return true;
        auto min_num = std::min(lhs.size(), rhs.size());
        for (size_t i = 0; i < min_num; ++i) {
            if (lhs[i] != rhs[i])
                return lhs[i] < rhs[i];
        }
        return false;
    }

    #ifdef USE_FIXVEC_POOL
    friend void swap(FixVec &lhs, FixVec &rhs)
    {
        std::swap(lhs.data, rhs.data);
        std::swap(lhs.num, rhs.num);
    }
    #endif

    friend iterator begin(FixVec &fv) {return fv.begin();}
    friend iterator end(FixVec &fv) {return fv.end();}
    friend const_iterator begin(const FixVec &fv) {return fv.begin();}
    friend const_iterator end(const FixVec &fv) {return fv.end();}
    friend const_iterator cbegin(const FixVec &fv) {return fv.begin();}
    friend const_iterator cend(const FixVec &fv) {return fv.end();}
};
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
