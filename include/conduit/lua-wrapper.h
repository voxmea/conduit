
#ifndef ROANOKE_INCLUDE_LUA_WRAPPER_H_
#define ROANOKE_INCLUDE_LUA_WRAPPER_H_

#include <iostream>
#include <type_traits>
#include <string>
#include <memory>
#include <tuple>
#include <vector>
#include <deque>
#include <array>
#include <cstdint>
#include <cinttypes>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <stdlib.h>
#include "conduit-utility.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

namespace conduit
{
// pop_arg is used to convert an argument on the lua stack to a function's
// argument.

template <typename T>
typename std::enable_if<std::is_same<T, bool>::value, T>::type pop_arg(lua_State *L, int arg, T * = nullptr)
{
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4800)
#endif
    return static_cast<T>(lua_toboolean(L, arg));
#ifdef _MSC_VER
#pragma warning (pop)
#endif
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value && (!std::is_same<T, bool>::value), T>::type pop_arg(lua_State *L, int arg, T * = nullptr)
{
#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4800)
#endif
    return static_cast<T>(lua_tointeger(L, arg));
#ifdef _MSC_VER
#pragma warning (pop)
#endif
}

template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, T>::type pop_arg(lua_State *L, int arg, T * = nullptr)
{
    return static_cast<T>(lua_tonumber(L, arg));
}

// TODO: is this appropriate?
template <typename T>
typename std::enable_if<std::is_pointer<T>::value, T>::type pop_arg(lua_State *L, int arg, T * = nullptr)
{
    return reinterpret_cast<T>(lua_touserdata(L, arg));
}

template <typename T>
typename std::enable_if<std::is_same<T, std::string>::value, T>::type pop_arg(lua_State *L, int arg, T * = nullptr)
{
    const char *s = lua_tostring(L, arg);
    if (s == nullptr) {
        luaL_error(L, "could not convert argument to string");
        return "";
    }
    auto len = lua_rawlen(L, arg);
    return std::string(s, len);
}

namespace detail
{
    template <typename T, typename = void>
    struct HasPopArg : std::false_type {};

    template <typename T>
    struct HasPopArg<T, void_t<decltype(pop_arg(std::declval<lua_State *&>(), std::declval<int &>(), std::declval<T *&>()))>> : std::true_type {};
}

template <typename K, typename V>
typename std::enable_if<detail::HasPopArg<K>::value && detail::HasPopArg<V>::value, std::map<K, V>>::type pop_arg(lua_State *L, int arg, std::map<K, V> * = nullptr)
{
    std::map<K, V> map;
    lua_pushnil(L);
    arg = (arg < 0) ? arg - 1 : arg;
    while (lua_next(L, arg)) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        map.emplace(pop_arg(L, -2, (K *)nullptr), pop_arg(L, -1, (V *)nullptr));
        lua_pop(L, 3);
    }
    return map;
}

template <typename K, typename V>
typename std::enable_if<detail::HasPopArg<K>::value && detail::HasPopArg<V>::value, std::unordered_map<K, V>>::type pop_arg(lua_State *L, int arg, std::unordered_map<K, V> * = nullptr)
{
    std::unordered_map<K, V> map;
    lua_pushnil(L);
    arg = (arg < 0) ? arg - 1 : arg;
    while (lua_next(L, arg)) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        map.emplace(pop_arg(L, -2, (K *)nullptr), pop_arg(L, -1, (V *)nullptr));
        lua_pop(L, 3);
    }
    return map;
}

template <typename T>
typename std::enable_if<detail::HasPopArg<T>::value, std::vector<T>>::type pop_arg(lua_State *L, int arg, std::vector<T> * = nullptr)
{
    std::vector<T> vec;
    lua_pushnil(L);
    arg = (arg < 0) ? arg - 1 : arg;
    while (lua_next(L, arg)) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        vec.emplace_back(pop_arg(L, -1, (T *)nullptr));
        lua_pop(L, 3);
    }
    return vec;
}

// push_arg is used to convert from c++ type to a value on the lua stack.
template <typename T>
typename std::enable_if<std::is_integral<std::decay_t<T>>::value, void>::type push_arg(lua_State *L, T &&arg)
{
    lua_pushinteger(L, static_cast<lua_Integer>(arg));
}

inline void push_arg(lua_State *L, bool b)
{
    lua_pushboolean(L, static_cast<int>(b));
}

template <typename T>
typename std::enable_if<std::is_floating_point<std::decay_t<T>>::value, void>::type push_arg(lua_State *L, T &&arg)
{
    lua_pushnumber(L, static_cast<lua_Number>(arg));
}

template <typename T>
void push_arg(lua_State *L, T *arg)
{
    lua_pushlightuserdata(L, reinterpret_cast<void *>(arg));
}

inline void push_arg(lua_State *L, const char *arg)
{
    lua_pushstring(L, arg);
}

template <typename T>
typename std::enable_if<std::is_same<std::decay_t<T>, std::string>::value, void>::type push_arg(lua_State *L, T &&arg)
{
    lua_pushlstring(L, arg.c_str(), arg.size());
}

// Important that this function takes Ts by value. If takes rvalue reference
// then ADL becomes a huge pain, requiring the user to define versions for
// rvalues, lvalues, and const refs.
namespace lua_detail {
template <typename T, typename ...U> struct FirstType {using type = T;};
}
template <typename ...T>
void push_arg(lua_State *L, T ...)
{
    static_assert(sizeof...(T) == 1, "Generic push_arg requires only one argument");
    lua_pushstring(L, conduit::demangle(typeid(typename lua_detail::FirstType<T...>::type).name()).c_str());
}

template <typename T, typename U>
void push_arg(lua_State *L, const std::map<T, U> &m)
{
    lua_newtable(L);
    for (auto &p : m) {
        push_arg(L, p.first);
        push_arg(L, p.second);
        lua_settable(L, -3);
    }
}

template <typename T, typename U>
void push_arg(lua_State *L, const std::unordered_map<T, U> &m)
{
    lua_newtable(L);
    for (auto &p : m) {
        push_arg(L, p.first);
        push_arg(L, p.second);
        lua_settable(L, -3);
    }
}

template <typename T>
void push_arg(lua_State *L, const std::vector<T> &v)
{
    lua_newtable(L);
    int i = 0;
    for (auto &j : v) {
        push_arg(L, j);
        lua_rawseti(L, -2, ++i);
    }
}

template <typename T>
void push_arg(lua_State *L, const std::deque<T> &v)
{
    lua_newtable(L);
    int i = 0;
    for (auto &j : v) {
        push_arg(L, j);
        lua_rawseti(L, -2, ++i);
    }
}

template <typename T, size_t SIZE>
void push_arg(lua_State *L, const std::array<T, SIZE> &a)
{
    lua_newtable(L);
    int i = 0;
    for (auto &j : a) {
        push_arg(L, j);
        lua_rawseti(L, -2, ++i);
    }
}

template <size_t SIZE, typename ...T>
struct TuplePushArg
{
    static void push_arg(lua_State *L, const std::tuple<T...> &t)
    {
        using conduit::push_arg;
        push_arg(L, std::get<(std::tuple_size<std::tuple<T...>>::value - 1) - (SIZE - 1)>(t));
        TuplePushArg<SIZE - 1, T...>::push_arg(L, t);
    }
};
template <typename ...T>
struct TuplePushArg<0, T...>
{
    static void push_arg(lua_State *L, const std::tuple<T...> &t)
    {
    }
};

template <typename ...T>
void push_arg(lua_State *L, const std::tuple<T...> t)
{
    TuplePushArg<std::tuple_size<std::tuple<T...>>::value, T...>::push_arg(L, t);
}

template <typename T>
T get_global(lua_State *L, const std::string &name)
{
    lua_getglobal(L, name.c_str());
    auto ret = pop_arg(L, -1, (T *)nullptr);
    lua_pop(L, 1);
    return ret;
}

template <typename T>
void set_global(lua_State *L, const std::string &name, T &&val)
{
    push_arg(L, std::forward<T>(val));
    lua_setglobal(L, name.c_str());
}

namespace detail
{
    inline std::tuple<std::string, std::string> ldsplit(const std::string &s)
    {
        auto pos = s.find('.');
        auto left = s.substr(0, pos);
        if (pos < s.size())
            ++pos;
        auto right = pos < s.size() ? s.substr(pos) : std::string("");
        return std::make_tuple(left, right);
    }
}

template <typename T>
void set_field(lua_State *L, const std::string &field, T &&t)
{
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    if (token.size() == 0 || rest.size() == 0) {
        set_global(L, field, std::forward<T>(t));
        return;
    }
    const int top = lua_gettop(L);
    lua_getglobal(L, token.c_str());
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, token.c_str());
        lua_getglobal(L, token.c_str());
    }
    while (rest.find('.') != std::string::npos) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setfield(L, -2, token.c_str());
            lua_getfield(L, -1, token.c_str());
        }
    }
    token = rest;
    push_arg(L, t);
    lua_setfield(L, -2, token.c_str());
    lua_settop(L, top);
}

inline void stack_dump(lua_State *L);

// This version, the value is already on the Lua stack, just set the name
inline void set_field(lua_State *L, const std::string &field)
{
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    if (token.size() == 0 || rest.size() == 0) {
        lua_setglobal(L, field.c_str());
        return;
    }
    const int top = lua_gettop(L);
    lua_getglobal(L, token.c_str());
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, token.c_str());
        lua_getglobal(L, token.c_str());
    }
    while (rest.find('.') != std::string::npos) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setfield(L, -2, token.c_str());
            lua_getfield(L, -1, token.c_str());
        }
    }
    token = rest;
    // rotate what was on the stack to the top
    for (int i = top; i < lua_gettop(L); ++i)
        lua_insert(L, top);
    lua_setfield(L, -2, token.c_str());
    lua_settop(L, top - 1);
}

template <typename T>
T get_field(lua_State *L, const std::string &field, T def = T())
{
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    const int top = lua_gettop(L);
    if (token.size() == 0 || rest.size() == 0)
        goto set_field;
    lua_getglobal(L, token.c_str());
    if (lua_isnil(L, -1)) {
        lua_settop(L, top);
        return def;
    }
    while (rest.size()) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1)) {
            lua_settop(L, top);
            goto set_field;
        }
    }
    {
        T ret = pop_arg(L, -1, (T *)nullptr);
        lua_settop(L, top);
        return ret;
    }

set_field:
    lua_settop(L, top);
    set_field(L, field, def);
    return def;
}

struct LuaWrapperBase
{
    virtual ~LuaWrapperBase() { }
    virtual void operator ()() = 0;

    static inline std::unordered_map<std::string, std::unique_ptr<LuaWrapperBase>> &functions()
    {
        static std::unordered_map<std::string, std::unique_ptr<LuaWrapperBase>> f;
        return f;
    }
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

template <typename C, typename R, typename ...PARAMS>
struct LuaWrapper final : LuaWrapperBase
{
    lua_State *L;
    C c;
    LuaWrapper(lua_State *L_, const C &c_) : L(L_), c(c_) { }

    template <int Index>
    struct GetArg
    {
        static typename std::tuple_element<Index, std::tuple<std::decay_t<PARAMS>...>>::type get_arg(lua_State *L)
        {
            using conduit::pop_arg;
            return pop_arg(L, Index + 1, (typename std::tuple_element<Index, std::tuple<std::decay_t<PARAMS>...>>::type *)nullptr);
        }
    };

    template <std::size_t ...I>
    R call_(std::index_sequence<I...>)
    {
        std::tuple<std::decay_t<PARAMS>...> args{GetArg<I>::get_arg(L)...};
        lua_pop(L, (int)sizeof...(PARAMS));
        return c(std::get<I>(args)...);
    }

    void operator ()() override
    {
        if (lua_gettop(L) < static_cast<int>(sizeof...(PARAMS))) {
            luaL_error(L, "invalid number of arguments");
            return;
        }
        // TODO: expand tuples.
        auto &&ret = call_(typename std::index_sequence_for<PARAMS...>{});
        push_arg(L, ret);
    }
};

template <typename C, typename ...PARAMS>
struct LuaWrapper<C, void, PARAMS...> final : LuaWrapperBase
{
    lua_State *L;
    C c;
    LuaWrapper(lua_State *L_, const C &c_) : L(L_), c(c_) { }

    template <int Index>
    struct GetArg
    {
        static typename std::tuple_element<Index, std::tuple<std::decay_t<PARAMS>...>>::type get_arg(lua_State *L)
        {
            return pop_arg(L, Index + 1, (typename std::tuple_element<Index, std::tuple<std::decay_t<PARAMS>...>>::type *)nullptr);
        }
    };

    template <std::size_t ...I>
    void call_(std::index_sequence<I...>)
    {
        std::tuple<std::decay_t<PARAMS>...> args{GetArg<I>::get_arg(L)...};
        lua_pop(L, (int)sizeof...(PARAMS));
        c(std::get<I>(args)...);
    }

    void operator ()() override
    {
        if (lua_gettop(L) < static_cast<int>(sizeof...(PARAMS))) {
            luaL_error(L, "invalid number of arguments");
            return;
        }
        call_(typename std::index_sequence_for<PARAMS...>{});
        // TODO: don't push anything, add another function to LuaWrapperBase to indicate number of args.
    }
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

struct LuaWrapperCaller
{
    static int call_lua_wrapper(lua_State *L)
    {
        LuaWrapperBase *lw = reinterpret_cast<LuaWrapperBase *>(lua_touserdata(L, lua_upvalueindex(1)));
        lw->operator ()();
        return lua_gettop(L);
    }
};

template <typename R, typename ...PARAMS>
std::unique_ptr<LuaWrapperBase> add_function(std::true_type, lua_State *L, R(*t)(PARAMS...))
{
    return std::make_unique<LuaWrapper<decltype(t), R, PARAMS...>>(L, t);
}

template <typename T, typename R, typename ...PARAMS>
std::unique_ptr<LuaWrapperBase> add_function(std::false_type, lua_State *L, T t, R(T::*)(PARAMS...))
{
    auto l = [t] (PARAMS... params) mutable {
        return t(params...);
    };
    return std::make_unique<LuaWrapper<decltype(l), R, PARAMS...>>(L, l);
}

template <typename T, typename R, typename ...PARAMS>
std::unique_ptr<LuaWrapperBase> add_function(std::false_type, lua_State *L, T t, R(T::*)(PARAMS...) const)
{
    auto l = [t] (PARAMS ...params) {
        return t(params...);
    };
    return std::make_unique<LuaWrapper<decltype(l), R, PARAMS...>>(L, l);
}

// This overload is for clang on windows where functors and lambdas are using
// different calling conventions (and hence the regular member function
// pointer does not match - not sure this should be part of the signature)!
// I'm pretty sure this is referenced on the Clang MSVC compatibility page and
// will be fixed someday. Not sure yet how to separate this out.
#if 0
#if defined(_MSC_VER) && !defined(__clang__)
template <typename T, typename R, typename ...PARAMS>
std::unique_ptr<LuaWrapperBase> add_function(std::false_type, lua_State *L, T &&t, R(__cdecl T::*f)(PARAMS...) const)
{
    auto l = [t] (PARAMS ...params) {
        return t(params...);
    };
    return std::make_unique<LuaWrapper<decltype(l), R, PARAMS...>>(L, l);
}
#endif
#endif

template <typename T>
std::unique_ptr<LuaWrapperBase> add_function(std::false_type, lua_State *L, T t)
{
    return add_function(std::false_type(), L, std::forward<T>(t), &T::operator ());
}

template <typename T>
std::string add_function(lua_State *L, const std::string &name, T t)
{
    using T_ = typename std::remove_pointer<std::decay_t<T>>::type;
    auto wrapper = add_function(typename std::is_function<T_>::type(), L, t);
    lua_pushlightuserdata(L, reinterpret_cast<void *>(wrapper.get()));
    lua_pushcclosure(L, LuaWrapperCaller::call_lua_wrapper, 1);
    set_field(L, name);
    LuaWrapperBase::functions().insert(std::make_pair(name, std::move(wrapper)));
    return name;
}

inline bool run_string(lua_State *L, const std::string &s, bool exit_on_error = false)
{
    int error = luaL_dostring(L, s.c_str());
    if (error) {
        std::cerr << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        if (exit_on_error)
            ::exit(1);
        return false;
    }
    return true;
}

inline void call_push_arg(lua_State *)
{
}

template <typename T>
void call_push_arg(lua_State *L, T &&t)
{
    push_arg(L, std::forward<T>(t));
}

template <typename T, typename ...U>
void call_push_arg(lua_State *L, T &&t, U &&...u)
{
    push_arg(L, std::forward<T>(t));
    call_push_arg(L, std::forward<U>(u)...);
}

template <typename ...T>
void call_function(lua_State *L, const std::string &name, T &&...t)
{
    lua_getglobal(L, name.c_str());
    call_push_arg(L, std::forward<T>(t)...);
    if (lua_pcall(L, sizeof...(T), 0, 0)) {
        std::cerr << "ERROR calling \"" << name << "\"\n";
        std::cerr << lua_tostring(L, -1) << std::endl;
        std::abort();
    }
}

template <typename R>
struct CallFunctionReturn
{
    static R get_ret(lua_State *L)
    {
        auto &&ret = pop_arg(L, -1, (R *)nullptr);
        lua_pop(L, 1);
        return ret;
    }
    static R get_ret(lua_State *L, int idx)
    {
        auto &&ret = pop_arg(L, -1, (R *)nullptr);
        lua_settop(L, idx);
        return ret;
    }
};

template <>
struct CallFunctionReturn<void>
{
    static void get_ret(lua_State *)
    {
    }
    static void get_ret(lua_State *L, int idx)
    {
        lua_settop(L, idx);
    }
};

template <typename R>
struct CallFunctionReturnSize
{
    static const int size = 1;
};

template <>
struct CallFunctionReturnSize<void>
{
    static const int size = 0;
};

template <typename R>
R call_function(lua_State *L, const std::string &name)
{
    lua_getglobal(L, name.c_str());
    if (lua_pcall(L, 0, CallFunctionReturnSize<R>::size, 0) != 0) {
        std::cerr << "ERROR calling \"" << name << "\"\n";
        std::cerr << lua_tostring(L, -1) << std::endl;
        std::abort();
    }
    return CallFunctionReturn<R>::get_ret(L);
}

template <typename R, typename ...T>
R call_function(lua_State *L, const std::string &name, T &&...t)
{
    lua_getglobal(L, name.c_str());
    call_push_arg(L, std::forward<T>(t)...);
    if (lua_pcall(L, sizeof...(T), CallFunctionReturnSize<R>::size, 0) != 0) {
        std::cerr << "ERROR calling \"" << name << "\"\n";
        std::cerr << lua_tostring(L, -1) << std::endl;
        std::abort();
    }
    return CallFunctionReturn<R>::get_ret(L);
}

struct FunctionWrapper
{
    lua_State *L;
    int ref;
    FunctionWrapper(lua_State *L_) : L(L_), ref(-1)
    {
        if (!lua_isfunction(L, -1)) {
            luaL_error(L, "invalid function in stack");
        } else {
            ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }

    FunctionWrapper(const FunctionWrapper &) = delete;

    FunctionWrapper(FunctionWrapper &&other) : L(other.L), ref(other.ref)
    {
        other.ref = -1;
    }

    ~FunctionWrapper()
    {
        if (ref != -1) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
    }

    static int traceback(lua_State *L)
    {
        luaL_traceback(L, L, NULL, 1);
        lua_getglobal(L, "debug");
        lua_getfield(L, -1, "traceback");
        lua_pushvalue(L, 1);
        lua_pushinteger(L, 2);
        lua_call(L, 2, 1);
        printf("%s\n", lua_tostring(L, -1));
        push_arg(L, "\nexiting due to lua error\n");
        return 1;
    }

    template <typename R, typename ...T>
    R call(T &&...t)
    {
        int start_idx = lua_gettop(L);
        lua_pushcclosure(L, &FunctionWrapper::traceback, 0);
        int errfunc = lua_gettop(L);

        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        call_push_arg(L, std::forward<T>(t)...);
        if (lua_pcall(L, sizeof...(T), CallFunctionReturnSize<R>::size, errfunc)) {
            std::cerr << lua_tostring(L, -1) << std::endl;
            std::abort();
        }
        return CallFunctionReturn<R>::get_ret(L, start_idx);
    }

    template <typename R>
    R call()
    {
        int start_idx = lua_gettop(L);
        lua_pushcclosure(L, &FunctionWrapper::traceback, 0);
        int errfunc = lua_gettop(L);

        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_pcall(L, 0, CallFunctionReturnSize<R>::size, errfunc)) {
            std::cerr << lua_tostring(L, -1) << std::endl;
            std::abort();
        }
        return CallFunctionReturn<R>::get_ret(L, start_idx);
    }
};

inline bool field_is_nil(lua_State *L, const std::string &field)
{
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    if (token.size() == 0)
        return true;
    const int top = lua_gettop(L);
    lua_getglobal(L, token.c_str());
    if (lua_isnil(L, -1)) {
        lua_settop(L, top);
        return true;
    }
    while (rest.size()) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1)) {
            lua_settop(L, top);
            return true;
        }
    }
    lua_settop(L, top);
    return false;
}

inline bool table_field_is_nil(lua_State *L, int index, const std::string &name)
{
    lua_getfield(L, index, name.c_str());
    bool ret = lua_isnil(L, -1);
    lua_pop(L, 1);
    return ret;
}

template <typename L, typename R>
void set_table_field(lua_State *LS, const L &l, const R &r)
{
    push_arg(LS, l);
    push_arg(LS, r);
    lua_settable(LS, -3);
}

template <typename T>
void set_table_field(lua_State *L, int index, const std::string &field, T &&t)
{
    const auto top = lua_gettop(L);
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    if (token.empty() || rest.empty()) {
        push_arg(L, t);
        lua_setfield(L, index, field.c_str());
        lua_settop(L, top);
        return;
    }
    lua_getfield(L, index, token.c_str());
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setfield(L, index, token.c_str());
        lua_getfield(L, index, token.c_str());
    }
    while (rest.find('.') != std::string::npos) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_setfield(L, -2, token.c_str());
            lua_getfield(L, -1, token.c_str());
        }
    }
    token = rest;
    push_arg(L, t);
    lua_setfield(L, -2, token.c_str());
    lua_settop(L, top);
}

template <typename T>
T get_table_field(lua_State *L, int index, const std::string &field)
{
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    const int top = lua_gettop(L);
    if (token.size() == 0 || rest.size() == 0) {
        lua_getfield(L, index, field.c_str());
    } else {
        lua_getfield(L, index, token.c_str());
    }
    if (lua_isnil(L, -1)) {
        luaL_error(L, "could not find field");
    }
    while (rest.size()) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1))
            luaL_error(L, "could not find field");
    }
    auto &&ret = pop_arg(L, -1, (T *)nullptr);
    lua_settop(L, top);
    return ret;
}

template <typename T>
T get_optional_table_field(lua_State *L, int index, const std::string &field, T def = T())
{
    std::string token, rest;
    std::tie(token, rest) = detail::ldsplit(field);
    const int top = lua_gettop(L);
    lua_getfield(L, index, token.c_str());
    if (lua_isnil(L, -1)) {
        goto set_field;
    }
    while (rest.size()) {
        std::tie(token, rest) = detail::ldsplit(rest);
        lua_getfield(L, -1, token.c_str());
        if (lua_isnil(L, -1)) {
            goto set_field;
        }
    }
    {
        auto &&ret = pop_arg(L, -1, (T *)nullptr);
        lua_settop(L, top);
        return ret;
    }

set_field:
    lua_settop(L, top);
    set_table_field(L, index, field, def);
    return def;
}

struct LuaPop
{
    lua_State *L;
    int index;
    LuaPop(lua_State *L_, int index_) : L(L_), index(index_) {}

    template <typename N, typename T>
    const LuaPop &operator &(std::tuple<N, T &> t) const
    {
        std::get<1>(t) = get_table_field<typename std::decay<T>::type>(L, index, std::get<0>(t));
        return *this;
    }
};

struct LuaPush
{
    lua_State *L;
    int index;
    LuaPush(lua_State *L_, int index_) : L(L_), index(index_) {}

    template <typename N, typename T>
    const LuaPush &operator &(const std::tuple<N, T> &t) const
    {
        set_table_field(L, index, std::get<0>(t), std::get<1>(t));
        return *this;
    }
};

// TODO: make this return a string
inline void stack_dump_print_(lua_State *L, int i)
{
    int t = lua_type(L, i);
    switch (t) {
    case LUA_TSTRING:  /* strings */
        printf("`%s'", lua_tostring(L, i));
        break;
    case LUA_TBOOLEAN:  /* booleans */
        printf(lua_toboolean(L, i) ? "true" : "false");
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, i)) {
            printf("%lld", lua_tointeger(L, i));
        } else {
            printf("%g", lua_tonumber(L, i));
        }
        break;
    default:  /* other values */
        printf("%s", lua_typename(L, t));
        break;
    }
}

// Taken from the lua book
inline void stack_dump(lua_State *L)
{
    int i;
    int top = lua_gettop(L);
    for (i = 1; i <= top; i++) {  /* repeat for each level */
        int t = lua_type(L, i);
        switch (t) {
        case LUA_TSTRING:  /* strings */
        case LUA_TBOOLEAN:  /* booleans */
        case LUA_TNUMBER:
        default:
            stack_dump_print_(L, i);
            break;
        case LUA_TTABLE:
            printf("[");
            {
                bool first = true;
                lua_pushnil(L);
                while (lua_next(L, i)) {
                    if (!first) {
                        printf(", ");
                        first = false;
                    }
                    stack_dump_print_(L, -2);
                    printf("=");
                    stack_dump_print_(L, -1);
                    lua_pop(L, 1);
                }
            }
            printf("]");
            break;
        }
        printf("  ");  /* put a separator */
    }
    if (top)
        printf("\n");  /* end the listing */
}


}

#endif
