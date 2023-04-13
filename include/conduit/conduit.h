
#ifndef CONDUIT_CONDUIT_H_
#define CONDUIT_CONDUIT_H_

#include "botch.h"

#ifndef CONDUIT_NO_PYTHON
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG 1
#else
#include <Python.h>
#endif
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <pybind11/eval.h>
#endif
#include "optional.h"
#include "small-callable.h"
#include "conduit-utility.h"
#include "function.h"
#include <tuple>
#include <set>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <memory>
#include <utility>
#include <initializer_list>
#include <type_traits>
#include <iostream>
#include <sstream>
// #define CONDUIT_CHANNEL_TIMES
#ifdef CONDUIT_CHANNEL_TIMES
#include <chrono>
#include <ratio>
#endif

namespace conduit
{
namespace detail
{
    #ifndef CONDUIT_LOGGER
    #define CONDUIT_LOGGER *::conduit::detail::Debug::logger()
    struct Debug
    {
        static std::ostream *&logger()
        {
            static std::ostream *l = &std::cout;
            return l;
        }
    };
    #endif

    struct Names
    {
        #ifdef CONDUIT_SOURCE_STRING_INTERNING
        // do not call this directly, use the accessors below
        static std::unordered_map<uint64_t, std::string> &get_names()
        {
            // 0 is always the empty string
            static std::unordered_map<uint64_t, std::string> n{{0, ""}};
            return n;
        }

        static uint64_t get_id_for_string(const std::string &n)
        {
            auto &names = get_names();
            auto i = std::find_if(names.begin(), names.end(), [&n] (auto &p) {
                if (p.second == n)
                    return true;
                return false;
            });
            if (i == names.end()) {
                names[names.size()] = n;
                return names.size() - 1;
            } else {
                return i->first;
            }
        }

        static std::string get_string_for_id(uint64_t id)
        {
            auto &names = get_names();
            if (names.find(id) == names.end())
                return "";
            return names[id];
        }
        #else
        static std::string get_id_for_string(std::string n)
        {
            return n;
        }

        static std::string get_string_for_id(std::string n)
        {
            return n;
        }
        #endif
    };

    #ifdef CONDUIT_CHANNEL_TIMES
    struct Times
    {
        struct Collection
        {
            std::unordered_map<uint64_t, std::chrono::duration<int64_t, std::nano>> times;

            ~Collection()
            {
                std::vector<std::pair<uint64_t, uint64_t>> deco;
                deco.reserve(times.size());
                uint64_t total_time = 0;
                std::for_each(times.begin(), times.end(), [&deco, &total_time] (auto &p) {
                    auto t = std::chrono::duration_cast<std::chrono::microseconds>(p.second).count();
                    total_time += t;
                    deco.push_back(std::make_pair(t, p.first));
                });
                std::sort(deco.begin(), deco.end());
                for (auto &p : deco) {
                    std::cout << Names::get_string_for_id(p.second) << " : " << std::dec << p.first << " : " << ((static_cast<double>(p.first) * 100) / total_time) << "%\n";
                }
            }
        };

        static std::unordered_map<uint64_t, std::chrono::duration<int64_t, std::nano>> &get_times()
        {
            static Collection collection;
            return collection.times;
        }
    };
    #endif

    // print_arg is used to perform default printing, with the exception that
    // user defined types that do not have a print_arg overload will have
    // their type printed.

    template <typename T>
    typename std::enable_if<detail::has_putto_operator<T>::value>::type print_arg(std::ostream &stream, T &&t)
    {
        stream << t;
    }

    template <typename T>
    typename std::enable_if<!detail::has_putto_operator<T>::value>::type print_arg(std::ostream &stream, T &&t)
    {
        stream << demangle(typeid(T).name());
    }

    inline void call_print_arg(std::ostream &) {}

    template <typename T>
    void call_print_arg(std::ostream &stream, T &&t)
    {
        print_arg(stream, std::forward<T>(t));
    }

    template <typename T, typename... U>
    void call_print_arg(std::ostream &stream, T &&t, U &&... u)
    {
        print_arg(stream, std::forward<T>(t));
        stream << ", ";
        call_print_arg(stream, std::forward<U>(u)...);
    }
}

namespace detail
{
    struct ExactReturnTypeTag {};
    struct ConvertibleReturnTypeTag {};
    struct OptionalNullTypeTag {};

    template <typename Proposed, typename Actual>
    struct ReturnTypeTag
    {
        using type = typename std::conditional<std::is_convertible<Proposed, Actual>::value, ConvertibleReturnTypeTag, OptionalNullTypeTag>::type;
    };

    template <typename Actual>
    struct ReturnTypeTag<conduit::Optional<Actual>, Actual>
    {
        using type = ExactReturnTypeTag;
    };

    template <typename Actual>
    struct ReturnTypeTag<Actual, Actual>
    {
        using type = ExactReturnTypeTag;
    };
}

// Channel needs to know about Registrars so it can be a friend class.
struct Registrar;
template <typename... T> struct ChannelInterface;
template <typename... T> struct RegistryEntry;
struct ChannelBase {
    virtual ~ChannelBase() {}
};
template <typename ...T> struct Channel;
template <typename R, typename... T>
struct Channel<R(T...)> final : public ChannelBase
{
    virtual ~Channel() {}

    // Never copied, this is to prevent slicing and performance issues.
    // Use a ChannelInterface instead.
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    template <typename R_ = R, typename EnableRet = typename std::enable_if<std::is_same<R_, void>::value>::type>
    void operator()(const T &...t)
    {
        if (callbacks->empty()) {
            return;
        }

        #ifdef CONDUIT_CHANNEL_TIMES
        auto start = std::chrono::high_resolution_clock::now();
        #endif

        in_callbacks = true;
        for (auto &c : *callbacks) {
            c.cb(t...);
        }
        in_callbacks = false;
        if (!pending_unsubscribe.empty()) {
            unsubscribe_();
        }

        #ifdef CONDUIT_CHANNEL_TIMES
        auto end = std::chrono::high_resolution_clock::now();
        auto &t = detail::Times::get_times()[detail::Names::get_id_for_string(name)];
        t += end - start;
        #endif
    }

    template <typename R_ = R, typename EnableRet = typename std::enable_if<!std::is_same<R_, void>::value, R_>::type>
    std::vector<conduit::Optional<R>> operator()(const T &... t)
    {
        if (callbacks->empty()) {
            return std::vector<conduit::Optional<R>>();
        }

        ret.clear();

        #ifdef CONDUIT_CHANNEL_TIMES
        auto start = std::chrono::high_resolution_clock::now();
        #endif

        in_callbacks = true;
        for (auto &c : *callbacks) {
            ret.emplace_back(c.cb(t...));
        }
        in_callbacks = false;
        if (!pending_unsubscribe.empty()) {
            unsubscribe_();
        }

        if (!resolves->empty()) {
            in_resolves = true;
            for (auto &r : *resolves) {
                r.cb(ret);
            }
            in_resolves = false;
            if (!pending_unresolve.empty()) {
                unresolve_();
            }
        }

        #ifdef CONDUIT_CHANNEL_TIMES
        auto end = std::chrono::high_resolution_clock::now();
        auto &t = detail::Times::get_times()[detail::Names::get_id_for_string(name)];
        t += end - start;
        #endif
        return ret;
    }

    Registrar &registrar;

private:
    template <typename C>
    std::string subscribe(C &&c, std::string client_name, int group = 0)
    {
        BOTCH(in_callbacks, "Can't subscribe while in_callbacks");
        using C_RET = decltype(c(std::declval<const T>()...));
        subscribe_(std::forward<C>(c), client_name, group, typename detail::ReturnTypeTag<C_RET, R>::type());
        return client_name;
    }

    void unsubscribe(const std::string &client_name)
    {
        BOTCH(client_name.empty(), "no unsubscribes of unnamed clients");
        auto pos = std::find_if(callbacks->begin(), callbacks->end(), [client_name] (struct Channel<R(T...)>::Callback &cb) {
            return cb.name == client_name;
        });
        if (pos == callbacks->end()) {
            return;
        }
        pending_unsubscribe.push_back(std::distance(callbacks->begin(), pos));
        if (!in_callbacks) {
            unsubscribe_();
        }
    }

    void unsubscribe(size_t index)
    {
        if (index < callbacks->size()) {
            pending_unsubscribe.push_back(index);
        }
        if (!in_callbacks) {
            unsubscribe_();
        }
    }

    template <typename C>
    std::string resolve(C &&c, std::string client_name, int group = 0)
    {
        BOTCH(in_resolves, "Can't resolve while in_resolves");
        auto iter = std::upper_bound(resolves->begin(), resolves->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        resolves->insert(iter, Resolve{c, client_name, group});
        return client_name;
    }

    void unresolve(const std::string &client_name)
    {
        BOTCH(client_name.empty(), "no unsubscribes of unnamed clients");
        auto pos = std::find_if(resolves->begin(), resolves->end(), [client_name] (struct Channel<R(T...)>::Callback &cb) {
            return cb.name == client_name;
        });
        if (pos == resolves->end()) {
            return;
        }
        pending_unresolve.push_back(std::distance(resolves->begin(), pos));
        if (!in_callbacks) {
            unsubscribe_();
        }
    }

    void unresolve(size_t index)
    {
        if (index < resolves->size()) {
            pending_unresolve.push_back(index);
        }
        if (!in_callbacks) {
            unresolve_();
        }
    }

    // Callback definition
    using OperatorReturn = typename std::conditional<std::is_same<R, void>::value, void, std::vector<conduit::Optional<R>>>::type;
    using CallbackReturn = typename std::conditional<std::is_same<R, void>::value, void, conduit::Optional<R>>::type;
    struct Callback
    {
        conduit::Function<CallbackReturn(const T &...)> cb;
        std::string name;
        int group;
    };

    // Resolve definition
    using ResolveFunctionType = typename std::conditional<std::is_same<R, void>::value, void(), void(const std::vector<conduit::Optional<R>>)>::type;
    struct Resolve
    {
        conduit::Function<ResolveFunctionType> cb;
        std::string name;
        int group;
    };

    // data
    std::string name;
    mutable bool debug = false;

    bool in_callbacks = false;
    std::shared_ptr<std::vector<Callback>> callbacks = std::make_shared<std::vector<Callback>>();
    std::vector<size_t> pending_unsubscribe;

    bool in_resolves = false;
    std::shared_ptr<std::vector<Resolve>> resolves = std::make_shared<std::vector<Resolve>>();
    std::vector<size_t> pending_unresolve;

    // Only Registrars can create channels.
    Channel(Registrar &reg) : registrar(reg) {}
    friend struct Registrar;
    friend struct RegistryEntry<R(T...)>;
    friend struct ChannelInterface<R(T...)>;

    // No Optional<void>, so pick int
    using RetType = typename std::conditional<std::is_same<R, void>::value, int, conduit::Optional<R>>::type;
    std::vector<RetType> ret;

    template <typename C>
    void subscribe_(C &&c, const std::string &client_name, int group, detail::ExactReturnTypeTag)
    {
        auto iter = std::upper_bound(callbacks->begin(), callbacks->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        callbacks->insert(iter, Callback{std::forward<C>(c), client_name, group});
    }

    template <typename C>
    void subscribe_(C &&c, const std::string &client_name, int group, detail::ConvertibleReturnTypeTag)
    {
        auto capture = std::forward<C>(c);
        auto iter = std::upper_bound(callbacks->begin(), callbacks->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        callbacks->insert(iter, Callback{[capture] (const T &...t) mutable {return static_cast<R>(capture(t...));}, client_name, group});
    }

    template <typename C>
    void subscribe_(C &&c, const std::string &client_name, int group, detail::OptionalNullTypeTag)
    {
        auto capture = std::forward<C>(c);
        auto iter = std::upper_bound(callbacks->begin(), callbacks->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        callbacks->insert(iter, Callback{[capture] (const T &...t) mutable {capture(t...); return conduit::OptionalNull();}, client_name, group});
    }

    void unsubscribe_()
    {
        callbacks->erase(std::remove_if(callbacks->begin(), callbacks->end(), [this] (struct Channel<R(T...)>::Callback &cb) {
            return std::find(pending_unsubscribe.begin(), pending_unsubscribe.end(), &cb - &(*callbacks)[0]) != pending_unsubscribe.end();
        }), callbacks->end());
        pending_unsubscribe.clear();
    }

    void unresolve_()
    {
        resolves->erase(std::remove_if(resolves->begin(), resolves->end(), [this] (struct Channel<R(T...)>::Resolve &cb) {
            return std::find(pending_unresolve.begin(), pending_unresolve.end(), &cb - &(*resolves)[0]) != pending_unresolve.end();
        }), resolves->end());
        pending_unresolve.clear();
    }

    void erase(int index)
    {
        unsubscribe(index);
    }

    // Used by Lua and Python
    static constexpr bool has_arguments = sizeof...(T) > 0;

    void print_debug_impl(const std::string &source, const T &... t);
    void print_debug(const std::string &source, const T &... t)
    {
        if (debug) {
            print_debug_impl(source, t...);
        }
    }

    #ifndef CONDUIT_NO_PYTHON
    template <std::size_t ...I>
    void call_from_python_args(const std::string &source, pybind11::args args, std::index_sequence<I...>)
    {
        std::tuple<T...> tuple_args{args[I].cast<T>()...};
        print_debug(source, std::get<I>(tuple_args)...);
        this->operator()(std::get<I>(tuple_args)...);
    }

    template <bool ha = has_arguments>
    typename std::enable_if<ha, bool>::type call_from_python(const std::string &source, pybind11::args args)
    {
        call_from_python_args(source, args, std::index_sequence_for<T...>{});
        return true;
    }

    template <bool ha = has_arguments>
    typename std::enable_if<!ha, bool>::type call_from_python(const std::string &source, pybind11::args args)
    {
        if (args.size())
            throw std::invalid_argument("calling nullary function with arguments");
        print_debug(source);
        this->operator()();
        return true;
    }
    #endif
};

template <typename ...T> struct ChannelInterface;
template <typename R, typename ...T>
struct ChannelInterface<R(T...)>
{
    #ifdef CONDUIT_SOURCE_STRING_INTERNING
    uint64_t source_id;
    #else
    std::string source_id;
    #endif
    Channel<R(T...)> *channel;

    // helper for Merge below
    using signature_return_type = R;

    // Make sure we're a POD.
    ChannelInterface() = default;
    ~ChannelInterface() = default;

    typename Channel<R(T...)>::OperatorReturn operator()(const T &...t) const
    {
        auto &c = *channel;
        if (c.debug) {
            CONDUIT_LOGGER << detail::Names::get_string_for_id(source_id) << " -> " << c.registrar.name << "." << channel->name << "(";
            detail::call_print_arg(CONDUIT_LOGGER, t...);
            CONDUIT_LOGGER << ")\n";
        }
        if (c.callbacks->size() == 0) {
            return typename Channel<R(T...)>::OperatorReturn();
        }
        return c(t...);
    }

    size_t num_callbacks() const
    {
        return channel->callbacks->size();
    }

    std::vector<std::string> callbacks() const
    {
        return std::accumulate(
            channel->callbacks->begin(), channel->callbacks->end(), std::vector<std::string>{},
            [](std::vector<std::string> &vec, struct Channel<R(T...)>::Callback &cb) {
                vec.push_back(cb.name);
                return vec;
            });
    }

    std::string name() const
    {
        return channel->name;
    }

    bool &debug() const
    {
        return channel->debug;
    }

    friend bool operator ==(const ChannelInterface &l, const ChannelInterface &r)
    {
        return l.source_id == r.source_id && l.channel == r.channel;
    }
    friend bool operator !=(const ChannelInterface &l, const ChannelInterface &r)
    {
        return l.source_id != r.source_id || l.channel != r.channel;
    }
};

struct Optuple
{
    virtual void reset() = 0;
    virtual uint64_t get_state() = 0;
    virtual ~Optuple() {}
};

template <typename Callback, typename Data>
struct OptupleImpl : Optuple
{
    uint64_t state = 0;
    Data data;
    Callback callback;
    conduit::Function<void()> response;
    std::string entity_name;

    template <typename ...T> struct IndexForTuple;
    template <typename ...T> struct IndexForTuple<detail::Tuple<T...>> { using type = std::index_sequence_for<T...>; };

    template <int index> using enabled_type = typename std::enable_if<index != 0>::type;
    template <int, int, typename Enabled, typename ...> struct GenDataIndex;
    template <int index, int count, typename R, typename ...T, typename ...U> struct GenDataIndex<index, count, enabled_type<index>, ChannelInterface<R(T...)>, ChannelInterface<U>...>
    {
        static const int data_index = GenDataIndex<index - 1, count + sizeof...(T), void, ChannelInterface<U>...>::data_index;
    };

    template <int count, typename ...T> struct GenDataIndex<0, count, void, ChannelInterface<T>...>
    {
        static const int data_index = count;
    };

    template <size_t ...I>
    void fire(std::index_sequence<I...>)
    {
        callback(detail::TupleGetVal<I>::get(data)...);
        if (response) {
            response();
        }
        reset();
    }

    template <int index>
    int destroy()
    {
        using element_type = typename detail::TupleElement<index, Data>::element_type;
        if (state & (1ULL << index)) {
            detail::TupleGetVal<index>::get(data).~element_type();
        }
        return index;
    }

    template <int index, typename V>
    int create(V &&v)
    {
        using element_type = typename detail::TupleElement<index, decltype(data)>::element_type;
        new (&detail::TupleGet<index>::get(data).buf) element_type(std::forward<V>(v));
        return index;
    }

    template <int index, int data_index, uint64_t mask, typename R, typename ...T, size_t ...I>
    int subscribe(ChannelInterface<R(T...)> ci, std::index_sequence<I...>)
    {
        ci.channel->registrar.template subscribe<R(T...)>(ci.name(), [this] (T ...t) {
            (void) (std::initializer_list<int>{
                destroy<data_index + I>()...
            });
            (void) (std::initializer_list<int>{
                create<data_index + I>(std::move(t))...
            });
            state |= (1ULL << index);
            if (state == mask) {
                fire(typename IndexForTuple<Data>::type());
            }
        }, entity_name);
        return index;
    }

    template <int index, int data_index, uint64_t mask, typename R, size_t ...I>
    int subscribe(ChannelInterface<R()> ci, std::index_sequence<I...>)
    {
        ci.channel->registrar.template subscribe<R()>(ci.name(), [this] {
            state |= (1ULL << index);
            if (state == mask) {
                fire(typename IndexForTuple<Data>::type());
            }
        }, entity_name);
        return index;
    }

    template <size_t ...I, typename ...T>
    void init(std::index_sequence<I...>, ChannelInterface<T> ...cis)
    {
        const uint64_t mask = (1ULL << sizeof...(cis)) - 1;
        (void)(std::initializer_list<int>({
            subscribe<I, GenDataIndex<I, 0, void, ChannelInterface<T>...>::data_index, mask>(cis, typename CallableInfo<T>::seq_type())...
        }));
    }

    template <size_t ...I>
    void clear(std::index_sequence<I...>)
    {
        (void)(std::initializer_list<int>{
            destroy<I>()...
        });
    }

    void reset() override
    {
        clear(typename IndexForTuple<Data>::type());
        state = 0;
    }

    uint64_t get_state() override
    {
        return state;
    }

    template <typename ...T>
    OptupleImpl(Callback c, std::string entity, ChannelInterface<T> ...cis) : callback(c), entity_name(entity)
    {
        init(typename std::make_index_sequence<sizeof...(cis)>(), cis...);
    }
};

template <typename ...T> struct TupleConvert;
template <typename ...T> struct TupleConvert<std::tuple<T...>> { using type = detail::Tuple<T...>; };

template <typename C, typename ...T>
std::shared_ptr<Optuple> merge(C &&c, std::string entity, Function<void()> response, ChannelInterface<T> ...cis)
{
    static_assert(sizeof...(cis) <= 64, "optuple supports a maximum of 64 channels");
    using TupleCatType = typename TupleCat<typename CallableInfo<ChannelInterface<T>>::tuple_parameter_type...>::type;
    using Data = typename TupleConvert<TupleCatType>::type;
    using Callback = std::decay_t<C>;
    auto optuple = std::make_shared<OptupleImpl<Callback, Data>>(c, entity, cis...);
    optuple->response = std::move(response);
    return optuple;
}

template <typename C, typename ...T>
std::shared_ptr<Optuple> merge(C &&c, ChannelInterface<T> ...cis)
{
    return merge(c, "merge", Function<void()>{}, cis...);
}

template <typename C, typename ...T>
std::shared_ptr<Optuple> merge(C &&c, std::string entity, ChannelInterface<T> ...cis)
{
    return merge(c, entity, Function<void()>{}, cis...);
}

// Registry

struct RegistryEntryBase {
    std::type_index ti = std::type_index(typeid(void));
    RegistryEntryBase() = default;
    RegistryEntryBase(RegistryEntryBase &&) = default;
    virtual ~RegistryEntryBase(){};
    virtual std::string to_string() const = 0;
    virtual void erase_callback(int) = 0;
    virtual std::vector<std::string> callbacks() const = 0;
    virtual std::string name() const = 0;
    virtual bool get_debug() = 0;
    virtual void set_debug(bool) = 0;
    virtual void alias(Registrar &) = 0;

    #ifndef CONDUIT_NO_PYTHON
    virtual void add_python_callback(pybind11::function, const std::string &, int = 0) = 0;
    virtual void call_from_python(const std::string &, pybind11::args) = 0;
    #endif
};

template <typename R, typename... T>
struct RegistryEntry<R(T...)> final : RegistryEntryBase
{
    // can't use make_shared because of private constructor...
    Channel<R(T...)> channel;

    RegistryEntry(Registrar &reg) : channel{reg} {}
    ~RegistryEntry() override {}

    std::string to_string() const override { return demangle(typeid(R(T...)).name()); }
    void erase_callback(int index) override { channel.erase(index); }
    std::vector<std::string> callbacks() const override
    {
        std::vector<std::string> ret;
        std::for_each(channel.callbacks->begin(), channel.callbacks->end(), [&ret] (const auto &cb) {
            ret.push_back(cb.name);
        });
        return ret;
    }
    std::string name() const override {return channel.name;}
    bool get_debug() override {return channel.debug;}
    void set_debug(bool debug) override {channel.debug = debug;}
    void alias(Registrar &) override;

    #ifndef CONDUIT_NO_PYTHON
    void add_python_callback(pybind11::function func, const std::string &n, int group) override
    {
        channel.subscribe(func, n, group);
    }
    void call_from_python(const std::string &source, pybind11::args args) override {channel.call_from_python(source, args);}
    #endif
};

struct ViewBase
{
    std::type_index ti;
    ViewBase(std::type_index ti) : ti(ti) {}
    virtual ~ViewBase() {}
    virtual std::string to_string() const = 0;
    #ifndef CONDUIT_NO_PYTHON
    virtual void subscribe_from_python(pybind11::function, pybind11::str) = 0;
    #endif
};

template <typename ...T> struct View;
template <typename R, typename ...T>
struct View<R(T...)> : ViewBase
{
    conduit::Function<void(conduit::Function<R(const T &...)>, std::string)> subscribe_function;

    template <typename ...U> struct IsTuple : std::false_type {};
    template <typename ...U> struct IsTuple<std::tuple<U...>> : std::true_type {};

    template <typename U, typename V, typename Ret, typename ...Args>
    std::enable_if_t<!IsTuple<typename CallableInfo<V>::return_type>::value> set_subscribe_function(ChannelInterface<U> ci, V &&v, Ret(*)(Args...))
    {
        subscribe_function = [=] (conduit::Function<R(const T &...)> cb, std::string name) {
            ci.channel->registrar.template subscribe<U>(ci, [=] (const Args &...args) {
                return cb(v(args...));
            }, name);
        };
    }

    template <typename C, typename ...V, std::size_t ...I>
    decltype(auto) apply(C &&cb, const std::tuple<V...> &v, std::index_sequence<I...>)
    {
        return cb(std::get<I>(v)...);
    }

    template <typename U, typename V, typename Ret, typename ...Args>
    std::enable_if_t<IsTuple<typename CallableInfo<V>::return_type>::value> set_subscribe_function(ChannelInterface<U> ci, V &&v, Ret(*)(Args...))
    {
        subscribe_function = [=] (conduit::Function<R(const T &...)> cb, std::string name) {
            ci.channel->registrar.template subscribe<U>(ci, [=] (const Args &...args) {
                return this->apply(cb, v(args...), std::make_index_sequence<std::tuple_size<typename CallableInfo<V>::return_type>::value>{});
            }, name);
        };
    }

    template <typename U>
    View(ChannelInterface<U> ci) : ViewBase(typeid(R(T...)))
    {
        subscribe_function = [=] (conduit::Function<R(const T &...)> cb, std::string name) {
            ci.channel->registrar.template subscribe<U>(ci, cb, name);
        };
    }

    template <typename U, typename V>
    View(ChannelInterface<U> ci, V &&v) : ViewBase(typeid(R(T...)))
    {
        set_subscribe_function(ci, std::forward<V>(v), typename CallableInfo<V>::function_type{nullptr});
    }

    void subscribe(conduit::Function<R(const T &...)> cb, std::string name)
    {
        subscribe_function(cb, name);
    }

    std::string to_string() const override { return demangle(typeid(R(T...)).name()); }

    #ifndef CONDUIT_NO_PYTHON
    void subscribe_from_python(pybind11::function f, pybind11::str entity) override
    {
        subscribe([f] (T ...t) {f(t...);}, entity);
    }
    #endif
};

struct PendingViewBase
{
    // opt_ti will stay un-engaged if the pending view is from Python
    conduit::Optional<std::type_index> opt_ti;
    std::string entity;
    PendingViewBase(conduit::Optional<std::type_index> opt_ti, std::string entity) : opt_ti(opt_ti), entity(entity) {}
    virtual void subscribe(ViewBase &view) = 0;
    virtual ~PendingViewBase() {};
};

template <typename ...T> struct PendingView;
template <typename R, typename ...T>
struct PendingView<R(T...)> : PendingViewBase
{
    conduit::Function<R(const T &...)> cb;
    PendingView(conduit::Function<R(const T &...)> cb_, std::string entity_) : PendingViewBase(typeid(R(T...)), entity_), cb(cb_) {}

    void subscribe(ViewBase &view) override
    {
        dynamic_cast<View<R(T...)> &>(view).subscribe(cb, entity);
    }
};

#ifndef CONDUIT_NO_PYTHON
struct PythonPendingHolderTag {};
template <>
struct PendingView<PythonPendingHolderTag> : PendingViewBase
{
    pybind11::function func;
    PendingView(pybind11::function func_, std::string entity_) : PendingViewBase(OptionalNull(), entity_), func(func_) {}

    void subscribe(ViewBase &view) override
    {
        view.subscribe_from_python(func, entity);
    }
};
#endif

// Allow subset views on a channel through make_changer

namespace changer_detail
{
    template <int ...I> struct int_seq {};
    template <typename ...T> struct int_cat;
    template <int ...T, int ...U> struct int_cat<int_seq<T...>, int_seq<U...>> {using type = int_seq<T..., U...>;};

    template <typename ...T> struct type_list {};
    template <typename ...T> struct type_cat;
    template <typename ...T, typename ...U> struct type_cat<type_list<T...>, type_list<U...>> {using type = type_list<T..., U...>;};

    template <typename T, typename ...U> struct first_type {using type = T; using remainder = type_list<U...>;};
    template <typename T, typename ...U> struct first_type<type_list<T, U...>> {using type = T; using remainder = type_list<U...>;};
    template <typename ...T> using first_type_t = typename first_type<T...>::type;
    template <typename ...T> using remainder_t = typename first_type<T...>::remainder;

    template <int I, typename ...T> struct Matcher;
    template <int I, typename ...U> struct Matcher<I, type_list<>, type_list<U...>>
    {
        using type = type_list<>;
        using seq = int_seq<>;
    };
    template <int I, typename ...T> struct Matcher<I, type_list<T...>, type_list<>>
    {
        using type = type_list<>;
        using seq = int_seq<>;
    };
    template <int I> struct Matcher<I, type_list<>, type_list<>>
    {
        using type = type_list<>;
        using seq = int_seq<>;
    };
    template <int I, typename ...T, typename ...U> struct Matcher<I, type_list<T...>, type_list<U...>>
    {
        static constexpr int is_same = std::is_same<first_type_t<T...>, first_type_t<U...>>::value;
        using match = std::conditional_t<is_same, type_list<first_type_t<T...>>, type_list<>>;
        using left = remainder_t<T...>;
        using right = std::conditional_t<is_same, remainder_t<U...>, type_list<U...>>;
        using type = typename type_cat<match, typename Matcher<I + 1, left, right>::type>::type;
        using seq = typename int_cat<std::conditional_t<is_same, int_seq<I>, int_seq<>>, typename Matcher<I + 1, left, right>::seq>::type;
    };

    template <typename ...T> struct ChannelChanger;
    template <typename ...T, typename ...U, typename R, typename Ignored>
    struct ChannelChanger<Ignored(T...), R(U...)>
    {
        using type = typename Matcher<0, type_list<T...>, type_list<U...>>::type;
        using seq = typename Matcher<0, type_list<T...>, type_list<U...>>::seq;
        static_assert(std::is_same<type, type_list<U...>>::value, "incompatible mapping");

        conduit::Function<R(const U &...)> f;

        template <typename ...V, int ...I>
        R apply(const std::tuple<V...> &v, int_seq<I...>)
        {
            return f(std::get<I>(v)...);
        }

        template <typename ...V>
        R operator() (V &&...v)
        {
            return this->apply(std::make_tuple(v...), seq());
        }
    };

    // workaround for gcc
    template <typename ...T> struct SizeHelper;
    template <typename R, typename ...T> struct SizeHelper<R(T...)> { static constexpr int size = sizeof...(T); };
}

template <typename T, typename U>
auto make_changer(U &&u)
{
    return changer_detail::ChannelChanger<T, typename CallableInfo<std::decay_t<U>>::signature>{std::forward<U>(u)};
}

template <typename T, typename U>
void make_changer(ChannelInterface<T> ci, U &&u, const std::string &entity = "")
{
    ci.channel->registrar.subscribe(ci, make_changer<T>(std::forward<U>(u)), entity);
}

// Registrar is the thing that keeps track of all channels by address.

struct Registrar
{
    std::string name;
    std::unordered_map<std::string, std::unique_ptr<RegistryEntryBase>> map;
    std::unordered_map<std::string, std::unordered_map<std::type_index, std::unique_ptr<ViewBase>>> views;
    std::unordered_map<std::string, std::vector<std::unique_ptr<PendingViewBase>>> pending_views;

    struct TraceNode
    {
        enum Kind {
            CHANNEL,
            ENTITY
        } kind;
        std::string name;
    };
    std::vector<conduit::Function<void(TraceNode source, TraceNode dest, std::type_index)>> tracers;

    void trace(RegistryEntryBase *reb, TraceNode source, TraceNode dest)
    {
        std::for_each(tracers.begin(), tracers.end(), [reb, &source, &dest] (const auto &f) {
            f(source, dest, reb->ti);
        });
    }

    #ifndef CONDUIT_NO_PYTHON
    struct PyChannel
    {
        // this is the producer/consumer name
        std::string entity;
        RegistryEntryBase *reb;
    };

    struct PyView
    {
        std::string name;
        ViewBase *view;
        void subscribe(pybind11::function f, pybind11::str s) { view->subscribe_from_python(f, s); }
    };

    void init_python_bindings()
    {
        pybind11::module conduit = pybind11::reinterpret_borrow<pybind11::module>(PyImport_AddModule("conduit"));
        if (!hasattr(conduit, "Channel")) {
            pybind11::class_<PyChannel>(conduit, "Channel")
                .def_property_readonly("name", [] (PyChannel &pyc) {return pyc.reb->name();})
                .def_readwrite("extra_name", &PyChannel::entity)
                .def_property("debug", [] (PyChannel &pyc) {return pyc.reb->get_debug();}, [] (PyChannel &pyc, bool debug) {pyc.reb->set_debug(debug);})
                .def("consumers", [] (PyChannel &pyc) { return pyc.reb->callbacks(); })
                .def_property_readonly("signature", [] (const PyChannel &pyc) { return pyc.reb->to_string(); })
                .def("__call__", [] (PyChannel &pyc, pybind11::args args) {pyc.reb->call_from_python(pyc.entity, args);});
        }
        if (!hasattr(conduit, "View")) {
            pybind11::class_<PyView>(conduit, "View")
                .def_property_readonly("name", [] (PyView &pyv) { return pyv.name; })
                .def_property_readonly("signature", [] (const PyView &pyv) { return pyv.view->to_string(); })
                .def("subscribe", &PyView::subscribe);
        }
        if (!hasattr(conduit, "TraceNode")) {
            auto tn = pybind11::class_<TraceNode>(conduit, "TraceNode")
                .def_readwrite("kind", &TraceNode::kind)
                .def_readwrite("name", &TraceNode::name);
            pybind11::enum_<TraceNode::Kind>(tn, "Kind")
                .value("CHANNEL", TraceNode::CHANNEL)
                .value("ENTITY", TraceNode::ENTITY)
                .export_values();
        }
        if (!hasattr(conduit, "Registrar")) {
            auto pub = [] (Registrar &reg, pybind11::str n_, pybind11::str source) {
                std::string n = n_;
                if (reg.map.find(n) == reg.map.end()) {
                    std::ostringstream stream;
                    stream << "unable to find \"" << n << "\"";
                    throw pybind11::index_error(stream.str());
                }
                auto &reb = reg.map[n];
                reg.trace(reb.get(), TraceNode{TraceNode::ENTITY, static_cast<std::string>(source)}, TraceNode{TraceNode::CHANNEL, n});
                return PyChannel{static_cast<std::string>(source), reb.get()};
            };
            auto sub = [] (Registrar &reg, pybind11::str n_, pybind11::function func, pybind11::str target, int group) {
                std::string n = n_;
                if (reg.map.find(n) == reg.map.end()) {
                    std::ostringstream stream;
                    stream << "unable to find \"" << n << "\"";
                    throw pybind11::index_error(stream.str());
                }
                auto &reb = reg.map[n];
                reb->add_python_callback(func, target, group);
                reg.trace(reb.get(), TraceNode{TraceNode::CHANNEL, n}, TraceNode{TraceNode::ENTITY, target});
                return target;
            };
            auto channels = [] (Registrar &reg) {
                std::vector<PyChannel> ret;
                reg.visit([&ret] (auto &reb) {
                    ret.push_back(PyChannel{"temp", &reb});
                });
                return ret;
            };
            auto views = [] (Registrar &reg) {
                std::vector<PyView> ret;
                for (const auto &map_p : reg.views) {
                    for (const auto &p : map_p.second) {
                        ret.push_back(PyView{map_p.first, p.second.get()});
                    }
                }
                return ret;
            };
            pybind11::class_<Registrar, std::shared_ptr<Registrar>>(conduit, "Registrar")
                .def("publish", pub)
                .def("subscribe", sub, "subscribe to a channel",
                     pybind11::arg("name"), pybind11::arg("callback"), pybind11::arg("entity"), pybind11::arg("group") = 0)
                .def("channels", channels)
                .def("views", views)
                .def("trace", [] (Registrar &reg, pybind11::function func) {
                    reg.tracers.push_back([func] (TraceNode source, TraceNode dest, std::type_index index) {
                        func(source, dest, demangle(index.name()));
                    });
                })
                .def_readonly("name", &Registrar::name);
        }
    }
    #endif

    Registrar(const std::string &n_)
        : name(n_)
    {
        #ifndef CONDUIT_NO_PYTHON
        init_python_bindings();
        #endif
    }

    ~Registrar()
    {
        #if 0
        std::cout << "destroying " << name << '\n';
        #endif
    }

    Registrar(const Registrar &) = delete;
    Registrar &operator =(const Registrar &) = delete;

    template <typename ...U> struct FixType;
    template <typename R, typename ...U> struct FixType<R(U...)> {using type = R(std::decay_t<U>...);};

    template <typename T>
    RegistryEntry<T> &find(const std::string &name)
    {
        auto ti = std::type_index(typeid(T));
        if (!map[name]) {
            auto re = std::make_unique<RegistryEntry<T>>(*this);
            re->ti = ti;
            re->channel.name = name;
            map[name] = std::move(re);
        } else {
            auto &re = map[name];
            BOTCH(ti != re->ti, "ERROR: type mismatch for {} (registered {}, requested {})", name, re->to_string(), demangle(typeid(T).name()));
        }
        return static_cast<RegistryEntry<T> &>(*map[name]);
    }

    // TODO: fix the recursive loop so that swap(pending_views, copy) isn't necessary
    void match_pending_views(std::string name)
    {
        if (pending_views.find(name) == pending_views.end()) {
            return;
        }
        decltype(pending_views) copy;
        using std::swap;
        swap(pending_views, copy);;
        auto &pending_vec = copy[name];
        pending_vec.erase(pending_vec.begin(), std::partition(pending_vec.begin(), pending_vec.end(), [this, &name] (auto &pending) {
            bool ret = false;
            for (auto &pr : views[name]) {
                if (!pending->opt_ti.engaged() || *pending->opt_ti == pr.first) {
                    pending->subscribe(*pr.second);
                    ret = true;
                }
            }
            return ret;
        }));
        swap(copy, pending_views);
    }

    template <typename Sig_, typename ChanSig>
    std::string register_view(ChannelInterface<ChanSig> ci, std::string name)
    {
        if (&ci.channel->registrar != this) {
            throw conduit::ConduitError("Registrar mismatch");
        }
        using Sig = typename FixType<Sig_>::type;
        auto &ti_map = views[name];
        std::type_index new_ti{typeid(Sig)};
        if (ti_map.find(new_ti) == ti_map.end()) {
            ti_map[new_ti] = std::make_unique<View<Sig>>(ci);
        }
        match_pending_views(ci.name());
        return name;
    }

    template <typename Sig_, typename ChanSig, typename Trans>
    std::string register_view(ChannelInterface<ChanSig> ci, Trans &&trans, std::string name)
    {
        if (&ci.channel->registrar != this) {
            throw conduit::ConduitError("Registrar mismatch");
        }
        using Sig = typename FixType<Sig_>::type;
        auto &ti_map = views[name];
        std::type_index new_ti{typeid(Sig)};
        if (ti_map.find(new_ti) == ti_map.end()) {
            ti_map[new_ti] = std::make_unique<View<Sig>>(ci, std::forward<Trans>(trans));
        } else {
            CONDUIT_LOGGER << "WARNING: resetting view " << conduit::demangle(ci.name().c_str()) << ":" << typeid(Sig).name() << '\n';
            ti_map[new_ti] = std::make_unique<View<Sig>>(ci, std::forward<Trans>(trans));
        }
        match_pending_views(ci.name());
        return name;
    }

    template <typename T_>
    ChannelInterface<typename FixType<T_>::type> publish(const std::string &name, const std::string &source = "")
    {
        using T = typename FixType<T_>::type;
        static_assert(std::is_function<T_>::value, "publish must be passed a function type");
        auto &re = find<T>(name);
        auto *channel = &re.channel;

        trace(map[name].get(), TraceNode{TraceNode::ENTITY, source}, TraceNode{TraceNode::CHANNEL, name});
        auto ci = ChannelInterface<T>{detail::Names::get_id_for_string(source), channel};
        register_view<T>(ci, name);
        return ci;
    }

    template <typename T, typename U>
    void subscribe(Channel<T> *channel, U &&target, std::string target_name, std::true_type)
    {
        channel->subscribe(std::forward<U>(target), target_name);
        register_view<typename CallableInfo<std::decay_t<U>>::signature>(ChannelInterface<T>{detail::Names::get_id_for_string(""), channel}, channel->name);
    }

    template <typename T, typename U>
    void subscribe(Channel<T> *channel, U &&target, std::string target_name, std::false_type)
    {
        channel->subscribe(make_changer<T>(std::forward<U>(target)), target_name);
        register_view<typename CallableInfo<std::decay_t<U>>::signature>(ChannelInterface<T>{detail::Names::get_id_for_string(""), channel}, channel->name);
    }

    template <typename T_, typename U>
    std::string subscribe(const std::string &name, U &&target, const std::string &target_name = "")
    {
        using T = typename FixType<T_>::type;
        static_assert(std::is_function<T_>::value, "subscribe must be passed a function type");
        auto &re = find<T>(name);

        using same_size_t = std::conditional_t<changer_detail::SizeHelper<T>::size == changer_detail::SizeHelper<typename CallableInfo<std::decay_t<U>>::signature>::size,
                                               std::true_type,
                                               std::false_type>;
        subscribe(&re.channel, std::forward<U>(target), target_name, same_size_t{});
        trace(map[name].get(), TraceNode{TraceNode::CHANNEL, name}, TraceNode{TraceNode::ENTITY, target_name});
        return target_name;
    }

    template <typename T, typename U>
    std::string subscribe(ChannelInterface<T> ci, U &&target, const std::string &target_name = "")
    {
        BOTCH(&ci.channel->registrar != this, "Registrar mismatch");
        using same_size_t = std::conditional_t<changer_detail::SizeHelper<T>::size == changer_detail::SizeHelper<typename CallableInfo<std::decay_t<U>>::signature>::size,
                                               std::true_type,
                                               std::false_type>;
        subscribe(ci.channel, std::forward<U>(target), target_name, same_size_t{});
        trace(map[ci.name()].get(), TraceNode{TraceNode::CHANNEL, ci.name()}, TraceNode{TraceNode::ENTITY, target_name});
        return target_name;
    }

    template <typename U>
    std::string subscribe(std::string name, U &&u, std::string entity)
    {
        auto &ti_map = views[name];
        using Sig = typename FixType<typename CallableInfo<U>::signature>::type;
        std::type_index new_ti{typeid(Sig)};
        if (ti_map.find(new_ti) == ti_map.end()) {
            pending_views[name].emplace_back(std::make_unique<PendingView<Sig>>(u, entity));
        } else {
            dynamic_cast<View<Sig> *>(ti_map[new_ti].get())->subscribe(u, entity);
        }
        return entity;
    }

    #ifndef CONDUIT_NO_PYTHON
    std::string subscribe(std::string name, pybind11::function func, std::string entity)
    {
        if (views.find(name) == views.end()) {
            pending_views[name].emplace_back(std::make_unique<PendingView<PythonPendingHolderTag>>(func, entity));
            return entity;
        }

        auto &ti_map = views[name];
        for (auto &[ti, view_ptr] : ti_map) {
            view_ptr->subscribe_from_python(func, entity);
        }
        return entity;
    }
    #endif

    // NOTE! this operation is not transitive. To alias multiple channels you
    // must use the same base registrar!
    void alias(Registrar &reg, std::string name)
    {
        BOTCH(map.find(name) == map.end(), "alias channel must already exist");
        map[name]->alias(reg);
    }

    void set_debug(bool debug)
    {
        for (auto &p : map) {
            p.second->set_debug(debug);
        }
    }

    template <typename C>
    void visit(C &&c)
    {
        for (auto &p : map) {
            c(*p.second);
        }
    }
};

// Wraps a Registrar with pub/sub API that automatically adds entity names
struct ClientRegistrar
{
    Registrar &reg;
    std::string entity;

    template <typename T_>
    ChannelInterface<typename Registrar::FixType<T_>::type> publish(const std::string &name) const
    {
        return reg.publish<typename Registrar::FixType<T_>::type>(name, entity);
    }

    // access ChannelInterface without tracing (e.g. for merge)
    template <typename T_>
    ChannelInterface<typename Registrar::FixType<T_>::type> find(const std::string &name) const
    {
        using T = typename Registrar::FixType<T_>::type;
        return ChannelInterface<T>{detail::Names::get_id_for_string(entity), &reg.find<T>(name).channel};
    }

    template <typename T_, typename U_>
    std::string subscribe(const std::string &name, U_ &&target) const
    {
        return reg.subscribe<T_>(name, std::forward<U_>(target), entity);
    }

    template <typename T_, typename U_>
    std::string subscribe(ChannelInterface<T_> ci, U_ &&target) const
    {
        return reg.subscribe<T_>(ci, std::forward<U_>(target), entity);
    }

};

template <typename R, typename ...T>
inline void RegistryEntry<R(T...)>::alias(Registrar &reg)
{
    // this ensures the ore exists and agrees on types
    reg.publish<R(T...)>(channel.name);
    BOTCH(reg.map.find(channel.name) == reg.map.end(), "wha?");
    auto &up = reg.map[channel.name];
    auto &ore = *reinterpret_cast<RegistryEntry<R(T...)> *>(up.get());
    auto &oc = ore.channel;
    std::copy(oc.callbacks->begin(), oc.callbacks->end(), std::back_inserter(*channel.callbacks));
    std::copy(oc.resolves->begin(), oc.resolves->end(), std::back_inserter(*channel.resolves));
    ore.channel.callbacks = channel.callbacks;
    ore.channel.resolves = channel.resolves;
}

template <typename R, typename... T>
void Channel<R(T...)>::print_debug_impl(const std::string &source, const T &... t)
{
    CONDUIT_LOGGER << source << " -> " << registrar.name << "." << name << "(";
    detail::call_print_arg(CONDUIT_LOGGER, t...);
    CONDUIT_LOGGER << ")\n";
}

/**
   connect a single callback to multiple channels with varying signatures
   uses type matching if necessary
 */
template <typename C, typename ...U>
std::string subscribe(C &&callback, std::string name, U ...u)
{
    std::initializer_list<std::string>{u.channel->registrar->template subscribe<typename CallableInfo<U>::signature>(u, callback, name)...};
    return name;
}

template <typename T>
struct Observable
{
    struct Observer
    {
        Observable &ob;
        Observer(Observable &ob_) : ob(ob_) {}
        Observer(const Observer &) = default;
        ~Observer() { ob.cb(ob.t); }
        T &operator *() const { return ob.t; }
        T *operator ->() { return &ob.t; }
        const T *operator ->() const { return &ob.t; }
        operator const T &() const { return ob.read(); }
        Observer &operator =(const T &o) { ob.t = o; return *this; }

        template <typename U = T, typename Ret = decltype(std::declval<U&>()[0])>
        Ret operator [](size_t index) { return ob.t[index]; }

        template <typename U = T, typename Ret = decltype(++std::declval<U&>())>
        Ret operator ++() { return ++ob.t; }

        template <typename U = T, typename Ret = decltype(--std::declval<U&>())>
        Ret operator --() { return --ob.t; }

        template <typename ...U, typename V = T, typename Ret = decltype(std::declval<V&>()(std::declval<U&>()...))>
        Ret operator ()(U &&...u) { return ob.t(std::forward<U>(u)...); }

        template <typename U = T, typename Ret = decltype(std::begin(std::declval<U&>()))>
        friend Ret begin(const Observer &ob) { return std::begin(*ob); }
        template <typename U = T, typename Ret = decltype(std::end(std::declval<U&>()))>
        friend Ret end(const Observer &ob) { return std::end(*ob); }
    };
    friend Observer;

    template <typename U, typename V = T>
    Observable(U &&u, std::enable_if_t<std::is_default_constructible<V>::value, char> * = nullptr) : cb(std::forward<U>(u)) {}

    template <typename U, typename V>
    Observable(U &&u, V &&v) : cb(std::forward<U>(u)), t(std::forward<V>(v)) {}

    template <typename U = T>
    Observable(Registrar &reg, std::string channel_name, std::string entity_name, std::enable_if_t<std::is_default_constructible<U>::value, char> * = nullptr)
        : cb(reg.publish<void(T)>(channel_name, entity_name))
    {}

    // not copyable or assignable, because we're not including those as observable
    Observable(const Observable &) = delete;
    Observable &operator =(const Observable &) = delete;

    const T &read() const { return t; }
    Observer write() { return Observer(*this); }
    Observer operator *() { return Observer(*this); }
    Observer operator ->() { return Observer(*this); }

    template <typename U = T, typename Ret = decltype(std::cbegin(std::declval<U&>()))>
    friend Ret begin(const Observable &ob) { return std::cbegin(ob.t); }
    template <typename U = T, typename Ret = decltype(std::cend(std::declval<U&>()))>
    friend Ret end(const Observable &ob) { return std::cend(ob.t); }

private:
    conduit::Function<void(const T &)> cb;
    T t;
};

#define conduit_run_expand_(x, y) x ## y
#define conduit_run_expand(x, y) conduit_run_expand_(x, y)
#define conduit_run(...) std::string conduit_run_expand(conduit_unique_mem_subscribe_id_, __COUNTER__) = __VA_ARGS__

} // namespace conduit


#endif
