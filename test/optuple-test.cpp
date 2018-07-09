
#include <format.h>
#include <conduit/conduit.h>

#if defined(_MSC_VER)
#include <Windows.h>
#include <io.h>
#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma warning(pop)
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
#else
void backtrace() {}
#endif

struct Noisy
{
    Noisy() {fmt::print("{}\n", __FUNCTION__); backtrace();}
    Noisy(const Noisy &) {fmt::print("copy {}\n", __FUNCTION__); backtrace();}
    Noisy(Noisy &&) {fmt::print("move {}\n", __FUNCTION__); backtrace();}
    ~Noisy() {fmt::print("{}\n", __FUNCTION__); backtrace();}
    Noisy &operator =(const Noisy &) {fmt::print("{}\n", __FUNCTION__); return *this;}
    Noisy &operator =(Noisy &&) {fmt::print("{}\n", __FUNCTION__); return *this;}
};

struct NoCopy
{
    NoCopy() = default;
    NoCopy(const NoCopy &) = delete;
    NoCopy &operator =(const NoCopy &) = delete;
};

int main(int argc, char const *argv[])
{
    auto response = [] {
        fmt::print("response\n");
    };

    // sanity
    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("sanity 1 1 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             response,
                             reg.lookup<void(int)>("one"),
                             reg.lookup<void(int)>("two"));
        reg.lookup<void(int)>("one")(1);
        reg.lookup<void(int)>("two")(1);
    }

    // one
    {
        conduit::Registrar reg("test");
        auto c = [] {
            fmt::print("empty\n");
        };
        auto optuple = merge(c, reg.lookup<void()>("one"));
        reg.lookup<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("single 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c, reg.lookup<void(int, int)>("one"));
        reg.lookup<void(int, int)>("one")(1, 2);
    }

    // two
    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("double 1 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.lookup<void()>("one"),
                             reg.lookup<void(int)>("two"));
        reg.lookup<void()>("one")();
        optuple->reset();
        reg.lookup<void(int)>("two")(1);
        reg.lookup<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("double 1 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.lookup<void(int)>("one"),
                             reg.lookup<void()>("two"));
        reg.lookup<void(int)>("one")(1);
        optuple->reset();
        reg.lookup<void()>("two")();
        reg.lookup<void(int)>("one")(1);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("double 1 1 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             response,
                             reg.lookup<void(int)>("one"),
                             reg.lookup<void(int)>("two"));
        optuple->reset();
        reg.lookup<void(int)>("one")(1);
        reg.lookup<void(int)>("two")(1);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] () {
            fmt::print("double empty\n");
        };
        auto optuple = merge(c,
                             response,
                             reg.lookup<void()>("one"),
                             reg.lookup<void()>("two"));
        optuple->reset();
        reg.lookup<void()>("one")();
        reg.lookup<void()>("two")();
    }

    // three
    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.lookup<void()>("one"),
                             reg.lookup<void(int)>("two"),
                             reg.lookup<void(int)>("three"));
        reg.lookup<void()>("one")();
        reg.lookup<void(int)>("two")(1);
        reg.lookup<void(int)>("three")(2);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.lookup<void(int)>("two"),
                             reg.lookup<void()>("one"),
                             reg.lookup<void(int)>("three"));
        reg.lookup<void(int)>("three")(2);
        reg.lookup<void()>("one")();
        reg.lookup<void(int)>("two")(1);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.lookup<void(int)>("two"),
                             reg.lookup<void(int)>("three"),
                             reg.lookup<void()>("one"));
        reg.lookup<void(int)>("two")(1);
        reg.lookup<void(int)>("three")(2);
        reg.lookup<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.lookup<void(int)>("two"),
                             reg.lookup<void(int)>("three"),
                             reg.lookup<void()>("one"));
        reg.lookup<void(int)>("two")(1);
        reg.lookup<void(int)>("three")(2);
        reg.lookup<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("three 2 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.lookup<void()>("two"),
                             reg.lookup<void(int)>("three"),
                             reg.lookup<void()>("one"));
        reg.lookup<void()>("two")();
        reg.lookup<void(int)>("three")(2);
        reg.lookup<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("three 2 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.lookup<void(int)>("three"),
                             reg.lookup<void()>("two"),
                             reg.lookup<void()>("one"));
        reg.lookup<void(int)>("three")(2);
        reg.lookup<void()>("two")();
        reg.lookup<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("three 2 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.lookup<void()>("two"),
                             reg.lookup<void()>("one"),
                             reg.lookup<void(int)>("three"));
        reg.lookup<void()>("two")();
        reg.lookup<void()>("one")();
        reg.lookup<void(int)>("three")(2);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] () {
            fmt::print("three empty\n");
        };
        auto optuple = merge(c,
                             reg.lookup<void()>("two"),
                             reg.lookup<void()>("one"),
                             reg.lookup<void()>("three"));
        reg.lookup<void()>("two")();
        reg.lookup<void()>("one")();
        reg.lookup<void()>("three")();
    }

    // noisy
    {
        conduit::Registrar reg("test");
        auto c = [] (Noisy) {
            fmt::print("noisy\n");
        };
        auto optuple = merge(c, reg.lookup<void(Noisy)>("one"));
        reg.lookup<void(Noisy)>("one")(Noisy());
    }

    // using channels for everything
    {
        conduit::Registrar reg("test");
        reg.lookup<void(int, int)>("forward").hook([] (int, int) {
            fmt::print("forward\n");
        });
        reg.lookup<void()>("token").hook([] {
            fmt::print("token\n");
        });
        auto optuple = merge(reg.lookup<void(int, int)>("forward"),
                             conduit::Function<void()>(reg.lookup<void()>("token")),
                             reg.lookup<void(int)>("one"),
                             reg.lookup<void(int)>("two"));
        reg.set_debug(true);
        reg.lookup<void(int)>("one")(1);
        reg.lookup<void(int)>("two")(2);
    }

    #if 0
    // nocopy
    {
        conduit::Registrar reg("test");
        auto c = [] (NoCopy) {
            fmt::print("nocopy\n");
        };
        auto optuple = merge(c, reg.lookup<void(NoCopy)>("one"));
        reg.lookup<void(NoCopy)>("one")(NoCopy());
    }
    #endif

    return 0;
}
