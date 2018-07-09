
#include <format.h>
#include <ostream.h>
#include <conduit/lua-wrapper.h>
#include "pipeline.h"
#include "events.h"
#include <conduit/repl.h>
#include <fstream>

#if defined(_MSC_VER)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma warning(pop)
#elif defined(__GNUC__) && defined(HAVE_LIBBACKTRACE)
#include <backtrace.h>
#endif

#if defined(_MSC_VER)
static void backtrace()
{
    auto p = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES);
    SymInitialize(p, NULL, TRUE);
    std::vector<void *> stack(100);
    auto num_frames = CaptureStackBackTrace(0, (DWORD)stack.size(), &stack[0], NULL);

    SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(new char[sizeof(SYMBOL_INFO) + 256 * sizeof(char)]);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    for (int i = 0; i < num_frames; i++) {
        SymFromAddr(p, (DWORD64)(stack[i]), 0, symbol);
        IMAGEHLP_LINE64 line{sizeof(IMAGEHLP_LINE64)};
        DWORD displacement = 0;
        SymGetLineFromAddr64(p, (DWORD64)stack[i], &displacement, &line);
        fmt::print("{}: {}({}:{} - {:x})\n", i, symbol->Name, line.FileName ? line.FileName : "", line.LineNumber, symbol->Address);
    }
    delete[] symbol;
}
#elif defined(__GNUC__) && defined(HAVE_LIBBACKTRACE)
static void backtrace()
{
    auto err = [](void *, const char *msg, int errnum) {
        fmt::print("BACKTRACE ERROR: {}: {}\n", errnum, msg);
    };
    auto state = backtrace_create_state(nullptr, 0, err, nullptr);
    if (state == nullptr) {
        fmt::print("could not create backtrace state\n");
        return;
    }

    auto callback = [](void *, uintptr_t pc, const char *filename, int lineno, const char *function) -> int {
        fmt::print("{}:{} - {}:{:#x}\n", roanoke::basename(filename), lineno, demangle(function), pc);
        return 0;
    };

    backtrace_full(state, 1, callback, err, nullptr);
}
#else
static void backtrace()
{
}
#endif

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

int main(int argc, const char *argv[])
{
    if (argc < 2) {
        fmt::print(std::cerr, "USAGE: {} INPUT1 [... INPUTN]\n", argv[0]);
        return -1;
    }

    auto ends_with = [](std::string s, std::string p) {
        auto i = s.rfind(p);
        return i != std::string::npos && ((i + p.size()) == s.size());
    };

    lua_State *L = luaL_newstate(); // NOLINT
    luaL_openlibs(L);
    conduit::lua::LuaGlobal::lua() = L;
    rl_attempted_completion_function = conduit::lua::completions;
    conduit::add_function(L, "backtrace", backtrace);
    conduit::Registrar reg("pipeline", L);

    for (auto &i : pe::SimInit::initializers())
        i.init(reg);

    auto stats = reg.lookup<void()>("stats", "main");
    // running lua must come after init because the channels must exist

    std::string input; 
    for (int i = 1; i < argc; ++i) {
        if (ends_with(argv[i], ".ops")) {
            if (!input.empty()) {
                fmt::print(std::cerr, "multiple input files not supported; exiting\n");
                return -1;
            }
            std::ifstream ifs(argv[i]);
            if (!ifs) {
                fmt::print(std::cerr, "error opening input file \"{}\"; exiting\n", argv[i]);
                return -1;
            }
            input.assign(std::istreambuf_iterator<char>(ifs),
                         std::istreambuf_iterator<char>());
        } else if (ends_with(argv[i], ".lua")) {
            run_lua_file(L, argv[i]);
        } else {
            fmt::print(std::cerr, "unknown input file type \"{}\"; exiting\n", argv[i]);
        }
    }

    if (input.empty()) {
        fmt::print(std::cerr, "no input; exiting\n");
        return 0;
    }

    bool end_simulation = false;
    reg.lookup<void()>("end simulation").hook([&end_simulation] {
        end_simulation = true;
    }, "main");

    conduit::ChannelInterface<void(std::string)> start = reg.lookup<void(std::string)>("start simulation", "main");
    ev::next(start, input);

    reg.visit([] (conduit::RegistryEntryBase &reb) {
        const auto &name = reb.name();
        if (name.empty())
            return;
        if (name[0] == '-')
            return;
        reb.set_debug(true);
    });

    auto &s = ev::detail::sched();
    while (!end_simulation) {
        BOTCH(s.empty(), "no events at {}\n", now);

        auto now = std::get<0>(s.front());
        auto &cycle = std::get<1>(s.front());

        fmt::print_colored(fmt::GREEN, "\n----{: ^80}----\n", now);

        for (auto iter = cycle.begin(); iter != cycle.end(); ++iter) 
            iter->ev();
        s.pop_front();
    }

    fmt::print_colored(fmt::BLUE, "\n\nsimulation ended\n\n");

    stats();

    return 0;
}
