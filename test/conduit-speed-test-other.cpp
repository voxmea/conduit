
#include <iostream>
#include <conduit/conduit.h>

using namespace conduit;

#if defined(__GNUC__) || defined(__clang__)
namespace roanoke {
template <class T>
void doNotOptimizeAway(T &&datum)
{
    asm volatile("" : "+r" (datum));
}
}
#else
#include <stdio.h>
#include <process.h>
namespace roanoke {
template <class T>
void doNotOptimizeAway(T &&datum)
{
    if (_getpid() == 1) {
        const void *p = &datum;
        putchar(*statc_cast<const char *>(p));
    }
}
}
#endif

struct Base {virtual int foo(int) = 0;};
struct Derived : Base {int foo(int i) override;};

int foo_calls = 0;
#ifdef _MSC_VER
__declspec(noinline)
#else
#include <sys/types.h>
#include <unistd.h>
#define _getpid getpid
int foo(int) __attribute__((noinline));
#endif
int foo(int i)
{
    ++foo_calls;
    auto ret = i + rand();
    if (static_cast<int>(_getpid()) == 1) {
        std::cout << ret << std::endl;
    }
    // doNotOptimizeAway(ret);
    return ret;
}

int Derived::foo(int i)
{
    return ::foo(i);
}

extern Registrar registrar;
void init()
{
    registrar.lookup<void(int)>("no return").hook([] (int i) {::foo(i);});
    registrar.lookup<int(int)>("int return").hook([] (int i) {::foo(i);});
}
