
#if 0
#include <format.h>
#include <ostream.h>
#include <conduit/conduit.h>
#include <conduit/lua-wrapper.h>
#include <unordered_map>

static int traceback(lua_State *L) // NOLINT
{
    luaL_traceback(L, L, NULL, 1);
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    fmt::print("{}\n", lua_tostring(L, -1));
    conduit::push_arg(L, "\nexiting due to lua error\n");
    return 1;
}

static void run_lua_file(lua_State *L, const std::string fn) // NOLINT
{
    lua_pushcclosure(L, &traceback, 0);
    int errfunc = lua_gettop(L);

    auto ret = luaL_loadfile(L, fn.c_str());
    if (ret) {
        fmt::print(std::cerr, "{}\n", conduit::pop_arg(L, -1, (std::string *)nullptr));
        ::exit(1);
    }
    ret = lua_pcall(L, 0, LUA_MULTRET, errfunc);
    if (ret) {
        fmt::print(std::cerr, "{}\n", conduit::pop_arg(L, -1, (std::string *)nullptr));
        ::exit(1);
    }
}

struct Foo
{
    int i;
    std::string s;
    double d;

    Foo() = delete;
    Foo(int i_, std::string s_, double d_) : i(i_), s(s_), d(d_) {}
    Foo(const Foo &) = default;

    friend void push_arg(lua_State *L, const Foo &f)
    {
        using conduit::set_table_field;
        lua_newtable(L);
        set_table_field(L, "i", f.i);
        set_table_field(L, "s", f.s);
        set_table_field(L, "d", f.d);
    }

    friend Foo pop_arg(lua_State *L, int index, const Foo * = nullptr)
    {
        using conduit::get_table_field;
        Foo f{get_table_field<int>(L, index, "i"),
              get_table_field<std::string>(L, index, "s"),
              get_table_field<double>(L, index, "d")};
        return f;
    }
};

int main(int argc, char *argv[])
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    conduit::lua::LuaGlobal::lua() = L;
    rl_attempted_completion_function = conduit::lua::completions;

    conduit::run_string(L, "function printf(s, ...)\n"
                       "io.write(s:format(...))\n"
                       "end\n");

    conduit::Registrar reg("test-reg", L);

    auto foo = reg.lookup<void(Foo, Foo)>("foo", "main");
    auto bar = reg.lookup<void()>("bar", "main");

    reg.lookup<void(Foo, Foo)>("foo").hook([] (Foo l, Foo r) {
        fmt::print("c - i={} s={} d={}\n", l.i, l.s, l.d);
        fmt::print("c - i={} s={} d={}\n", r.i, r.s, r.d);
    }, "main");

    reg.lookup<void()>("bar").hook([] {
        fmt::print("c got bar\n");
    }, "main");

    for (int i = 1; i < argc; ++i)
        run_lua_file(L, argv[i]);

    bar();
}

#endif
int main()
{
    
}
