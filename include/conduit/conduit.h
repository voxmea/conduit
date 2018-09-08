
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
        #ifdef SOURCE_STRING_INTERNING
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
        if (callbacks->empty())
            return;

        #ifdef CONDUIT_CHANNEL_TIMES
        auto start = std::chrono::high_resolution_clock::now();
        #endif

        in_callbacks = true;
        for (auto &c : *callbacks) {
            c.cb(t...);
        }
        in_callbacks = false;
        if (pending_unhook.size())
            unhook_();

        #ifdef CONDUIT_CHANNEL_TIMES
        auto end = std::chrono::high_resolution_clock::now();
        auto &t = detail::Times::get_times()[detail::Names::get_id_for_string(name)];
        t += end - start;
        #endif
    }

    template <typename R_ = R, typename EnableRet = typename std::enable_if<!std::is_same<R_, void>::value, R_>::type>
    std::vector<conduit::Optional<R>> operator()(const T &... t)
    {
        if (callbacks->empty())
            return std::vector<conduit::Optional<R>>();

        ret.clear();

        #ifdef CONDUIT_CHANNEL_TIMES
        auto start = std::chrono::high_resolution_clock::now();
        #endif

        in_callbacks = true;
        for (auto &c : *callbacks) {
            ret.emplace_back(c.cb(t...));
        }
        in_callbacks = false;
        if (pending_unhook.size())
            unhook_();

        if (resolves->size()) {
            in_resolves = true;
            for (auto &r : *resolves)
                r.cb(ret);
            in_resolves = false;
            if (pending_unresolve.size())
                unresolve_();
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
    std::string hook(C &&c, std::string client_name, int group = 0)
    {
        BOTCH(in_callbacks, "Can't hook while in_callbacks");
        using C_RET = decltype(c(std::declval<const T>()...));
        hook_(std::forward<C>(c), client_name, group, typename detail::ReturnTypeTag<C_RET, R>::type());
        return client_name;
    }

    void unhook(const std::string &client_name)
    {
        BOTCH(client_name.empty(), "no unhooks of unnamed clients");
        auto pos = std::find_if(callbacks->begin(), callbacks->end(), [client_name] (struct Channel<R(T...)>::Callback &cb) {
            return cb.name == client_name;
        });
        if (pos == callbacks->end())
            return;
        pending_unhook.push_back(std::distance(callbacks->begin(), pos));
        if (!in_callbacks) {
            unhook_();
        }
    }

    void unhook(size_t index)
    {
        if (index < callbacks->size())
            pending_unhook.push_back(index);
        if (!in_callbacks) {
            unhook_();
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
        BOTCH(client_name.empty(), "no unhooks of unnamed clients");
        auto pos = std::find_if(resolves->begin(), resolves->end(), [client_name] (struct Channel<R(T...)>::Callback &cb) {
            return cb.name == client_name;
        });
        if (pos == resolves->end())
            return;
        pending_unresolve.push_back(std::distance(resolves->begin(), pos));
        if (!in_callbacks) {
            unhook_();
        }
    }

    void unresolve(size_t index)
    {
        if (index < resolves->size())
            pending_unresolve.push_back(index);
        if (!in_callbacks) {
            unresolve_();
        }
    }

    // Callback definition
    using OperatorReturn = typename std::conditional<std::is_same<R, void>::value, void, std::vector<conduit::Optional<R>>>::type;
    using CallbackReturn = typename std::conditional<std::is_same<R, void>::value, void, conduit::Optional<R>>::type;
    struct Callback
    {
        std::function<CallbackReturn(const T &...)> cb;
        std::string name;
        int group;
    };

    // Resolve definition
    using ResolveFunctionType = typename std::conditional<std::is_same<R, void>::value, void(), void(const std::vector<conduit::Optional<R>>)>::type;
    struct Resolve
    {
        std::function<ResolveFunctionType> cb;
        std::string name;
        int group;
    };

    // data
    std::string name;
    mutable bool debug = false;

    bool in_callbacks = false;
    std::shared_ptr<std::vector<Callback>> callbacks = std::make_shared<std::vector<Callback>>();
    std::vector<size_t> pending_unhook;

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
    void hook_(C &&c, const std::string &client_name, int group, detail::ExactReturnTypeTag)
    {
        auto iter = std::upper_bound(callbacks->begin(), callbacks->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        callbacks->insert(iter, Callback{c, client_name, group});
    }

    template <typename C>
    void hook_(C &&c, const std::string &client_name, int group, detail::ConvertibleReturnTypeTag)
    {
        auto capture = c;
        auto iter = std::upper_bound(callbacks->begin(), callbacks->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        callbacks->insert(iter, Callback{[capture] (const T &...t) mutable {return static_cast<R>(capture(t...));}, client_name, group});
    }

    template <typename C>
    void hook_(C &&c, const std::string &client_name, int group, detail::OptionalNullTypeTag)
    {
        auto capture = c;
        auto iter = std::upper_bound(callbacks->begin(), callbacks->end(), group, [] (int group, const Callback &cb) {
            return group < cb.group;
        });
        callbacks->insert(iter, Callback{[capture] (const T &...t) mutable {capture(t...); return conduit::OptionalNull();}, client_name, group});
    }

    void unhook_()
    {
        callbacks->erase(std::remove_if(callbacks->begin(), callbacks->end(), [this] (struct Channel<R(T...)>::Callback &cb) {
            return std::find(pending_unhook.begin(), pending_unhook.end(), &cb - &(*callbacks)[0]) != pending_unhook.end();
        }), callbacks->end());
        pending_unhook.clear();
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
        unhook(index);
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
    #ifdef SOURCE_STRING_INTERNING
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
        if (c.callbacks->size() == 0)
            return typename Channel<R(T...)>::OperatorReturn();
        return c(t...);
    }

    template <typename C>
    std::string hook(C &&c, const std::string name = "", int group = 0) const
    {
        return channel->hook(std::forward<C>(c), name, group);
    }

    void unhook(std::string client_name)
    {
        channel->unhook(client_name);
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
    virtual ~Optuple() {}
};

template <typename Callback, typename Data>
struct OptupleImpl : Optuple
{
    detail::TupleState state;
    Data data;
    Callback callback;
    conduit::Function<void()> response;

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
        if (response)
            response();
        reset();
    }

    template <int index>
    int destroy()
    {
        using element_type = typename detail::TupleElement<index, Data>::element_type;
        if (state.val & (1ULL << index)) {
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
    int hook(ChannelInterface<R(T...)> ci, std::index_sequence<I...>)
    {
        ci.hook([this] (T ...t) {
            (void) (std::initializer_list<int>{
                destroy<data_index + I>()...
            });
            (void) (std::initializer_list<int>{
                create<data_index + I>(std::move(t))...
            });
            state.val |= (1ULL << index);
            if (state.val == mask) {
                fire(typename IndexForTuple<Data>::type());
            }
        }, "optuple");
        return index;
    }

    template <int index, int data_index, uint64_t mask, typename R, size_t ...I>
    int hook(ChannelInterface<R()> ci, std::index_sequence<I...>)
    {
        ci.hook([this] {
            state.val |= (1ULL << index);
            if (state.val == mask) {
                fire(typename IndexForTuple<Data>::type());
            }
        }, "optuple");
        return index;
    }

    template <size_t ...I, typename ...T>
    void init(std::index_sequence<I...>, ChannelInterface<T> ...cis)
    {
        const uint64_t mask = (1ULL << sizeof...(cis)) - 1;
        (void)(std::initializer_list<int>({
            hook<I, GenDataIndex<I, 0, void, ChannelInterface<T>...>::data_index, mask>(cis, typename CallableInfo<T>::seq_type())...
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
        state.val = 0;
    }

    template <typename ...T>
    OptupleImpl(Callback c, ChannelInterface<T> ...cis) : callback(c)
    {
        init(typename std::make_index_sequence<sizeof...(cis)>(), cis...);
    }
};

template <typename ...T> struct TupleConvert;
template <typename ...T> struct TupleConvert<std::tuple<T...>> { using type = detail::Tuple<T...>; };

template <typename C, typename ...T>
std::shared_ptr<Optuple> merge(C &&c, Function<void()> response, ChannelInterface<T> ...cis)
{
    static_assert(sizeof...(cis) <= 64, "optuple supports a maximum of 64 channels");
    using TupleCatType = typename TupleCat<typename CallableInfo<ChannelInterface<T>>::tuple_parameter_type...>::type;
    using Data = typename TupleConvert<TupleCatType>::type;
    using Callback = std::decay_t<C>;
    auto optuple = std::make_shared<OptupleImpl<Callback, Data>>(c, cis...);
    optuple->response = std::move(response);
    return optuple;
}

template <typename C, typename ...T>
std::shared_ptr<Optuple> merge(C &&c, ChannelInterface<T> ...cis)
{
    return merge(c, Function<void()>{}, cis...);
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
        channel.hook(func, n, group);
    }
    void call_from_python(const std::string &source, pybind11::args args) override {channel.call_from_python(source, args);}
    #endif
};

// Registrar is the thing that keeps track of all channels by address.

struct Registrar
{
    std::string name;
    std::unordered_map<std::string, std::unique_ptr<RegistryEntryBase>> map;
    std::vector<conduit::Function<void(std::string source, std::string dest, std::type_index)>> tracers;

    void trace(RegistryEntryBase *reb, const std::string &source, const std::string &dest)
    {
        std::for_each(tracers.begin(), tracers.end(), [reb, &source, &dest] (const auto &f) {
            f(source, dest, reb->ti);
        });
    }

    #ifndef CONDUIT_NO_PYTHON
    struct PyChannel
    {
        // this is the producer/consumer name
        std::string name;
        RegistryEntryBase *reb;
    };
    #endif

    Registrar(const std::string &n_)
        : name(n_)
    {
        #ifndef CONDUIT_NO_PYTHON
        pybind11::module m = pybind11::module::import("__main__");
        pybind11::module conduit = pybind11::reinterpret_borrow<pybind11::module>(PyImport_AddModule("conduit"));
        if (!hasattr(m, "conduit")) {
            setattr(m, "conduit", conduit);
        }
        if (!hasattr(conduit, "registrars")) {
            pybind11::dict reg;
            setattr(conduit, "registrars", reg);
        }
        if (!hasattr(conduit, "Channel")) {
            pybind11::class_<PyChannel>(conduit, "Channel")
                .def_property_readonly("name", [] (PyChannel &pyc) {return pyc.reb->name();})
                .def_readwrite("extra_name", &PyChannel::name)
                .def_property("debug", [] (PyChannel &pyc) {return pyc.reb->get_debug();}, [] (PyChannel &pyc, bool debug) {pyc.reb->set_debug(debug);})
                .def("__call__", [] (PyChannel &pyc, pybind11::args args) {pyc.reb->call_from_python(pyc.name, args);});
        }
        if (!hasattr(conduit, "Registrar")) {
            auto pub = [] (Registrar &reg, pybind11::str n_, pybind11::str source) {
                std::string n = n_;
                if (reg.map.find(n) == reg.map.end()) {
                    throw pybind11::index_error(fmt::format("unable to find \"{}\"\n", n));
                }
                auto &reb = reg.map[n];
                reg.trace(reb.get(), source, n);
                return PyChannel{static_cast<std::string>(source), reb.get()};
            };
            auto sub = [] (Registrar &reg, pybind11::str n_, pybind11::function func, pybind11::str target) {
                std::string n = n_;
                if (reg.map.find(n) == reg.map.end()) {
                    throw pybind11::index_error(fmt::format("unable to find \"{}\"\n", n));
                }
                auto &reb = reg.map[n];
                reb->add_python_callback(func, target);
                reg.trace(reb.get(), n, target);
                return target;
            };
            auto channels = [] (Registrar &reg) {
                std::vector<PyChannel> ret;
                reg.visit([&ret] (auto &reb) {
                    ret.push_back(PyChannel{"temp", &reb});
                });
                return ret;
            };
            pybind11::class_<Registrar, std::shared_ptr<Registrar>>(conduit, "Registrar")
                .def("publish", pub)
                .def("subscribe", sub)
                .def("channels", channels)
                .def_readonly("name", &Registrar::name);
        }
        #endif
    }

    template <typename ...U> struct FixType;
    template <typename R, typename ...U> struct FixType<R(U...)> {using type = R(std::decay_t<U>...);};

    template <typename T_>
    ChannelInterface<typename FixType<T_>::type> publish(const std::string &name, const std::string &source = "")
    {
        using T = typename FixType<T_>::type;
        static_assert(std::is_function<T_>::value, "publish must be passed a function type");

        auto ti = std::type_index(typeid(T));
        Channel<T> *channel = nullptr;
        if (!map[name]) {
            auto re = std::make_unique<RegistryEntry<T>>(*this);
            re->ti = ti;
            re->channel.name = name;
            channel = &re->channel;
            map[name] = std::move(re);
        } else {
            auto &re = map[name];
            BOTCH(ti != re->ti, "ERROR: type mismatch for {} (registered {}, requested {})", name, re->to_string(), demangle(typeid(T).name()));
            channel = &reinterpret_cast<RegistryEntry<T> *>(re.get())->channel;
        }
        trace(map[name].get(), source, name);
        return ChannelInterface<T>{detail::Names::get_id_for_string(source), channel};
    }

    template <typename T_, typename U_>
    std::string subscribe(const std::string &name, U_ &&target, const std::string &target_name = "")
    {
        using T = typename FixType<T_>::type;
        static_assert(std::is_function<T_>::value, "subscribe must be passed a function type");
        auto ci = publish<T>(name);
        ci.channel->hook(std::forward<U_>(target), target_name);
        trace(map[name].get(), name, target_name);
        return target_name;
    }

    template <typename T_, typename U_>
    std::string subscribe(ChannelInterface<T_> ci, U_ &&target, const std::string &target_name = "")
    {
        ci.channel->hook(target, target_name);
        trace(map[ci.name()].get(), ci.name(), target_name);
        return target_name;
    }

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

#define conduit_run_expand_(x, y) x ## y
#define conduit_run_expand(x, y) conduit_run_expand_(x, y)
#define conduit_run(...) std::string conduit_run_expand(conduit_unique_mem_hook_id_, __COUNTER__) = __VA_ARGS__

} // namespace conduit


#endif
