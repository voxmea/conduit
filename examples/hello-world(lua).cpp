
#if 0

#include <format.h>
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <random>

#ifdef _MSC_VER
#include <dbghelp.h>
void backtrace()
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
#endif

struct CustomType
{
    int data = 0;

    CustomType() = default;
    CustomType(int d_) : data(d_) {}
    operator int() const {return data;}
};

void opposite_printer(int value)
{
    fmt::print("{}", !value);
}

int main(int argc, char const *argv[])
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    conduit::lua::LuaGlobal::lua() = L;
    rl_attempted_completion_function = conduit::lua::completions;

    // Create a registrar. This represents a single namespace in which to find
    // channels and Registrars are the only way to create a channel. Components
    // in your model that communicate over channels would need to be passed the
    // same Registrar to find the required channels.
    conduit::Registrar reg("reg", L);

    // Lookup the "print channel". This provides a handle that can be used to
    // send messages on the channel.
    auto print = reg.lookup<void(CustomType)>("print channel", "producer");

    // Hook (subscribe, listen, etc.) on the print channel. We hook it twice
    // here to demonstrate any callable can be used to receive messages on a
    // channel (as long as it meets the requirements of the channel's function
    // signature).
    reg.lookup<void(CustomType)>("print channel").hook([] (int i) {
        fmt::print("{} ", i);
    }, "consumer");
    reg.lookup<void(CustomType)>("print channel").hook(opposite_printer, "opposite_consumer");

    // Finally, hook the print channel from Lua (note, the Lua here is embedded,
    // but this is a convenience only, and is identical to passing in a Lua
    // file)
    conduit::run_string(L, "conduit.registrars.reg.hook('print channel', function(s) io.write(','..s..',') end, 'lua consumer')");

    // generate a random string of 0s and 1s.
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(0, 1);
    std::vector<int> values(10);
    std::generate(values.begin(), values.end(), [&dist, &eng] () mutable {
        return dist(eng);
    });

    reg.set_debug(true);

    #ifdef _MSC_VER
    conduit::add_function(reg.L, "backtrace", backtrace);
    #endif

    // send a message on the print channel based on our random string of 0s and
    // 1s. Messages are sent on channels using the function call syntax.
    int count = 0;
    std::for_each(values.begin(), values.end(), [print, &count] (int value) {
        print(value);
        BOTCH(++count > 5, "count reached {}\n" , count);
        fmt::print("\n");
    });
}

#endif
int main()
{
    
}
