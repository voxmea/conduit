
#include <fmt/format.h>
#include <iostream>
#include <random>
#include <set>
#include <map>
#include <fstream>
#include <functional>
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define SOURCE_STRING_INTERNING
#include <conduit/conduit.h>
#include <conduit/function.h>
#include <chrono>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <Windows.h>
#include <process.h>
#else
#define _getpid getpid
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef SOURCE_STRING_INTERNING
#error
#endif

using namespace conduit;
Registrar registrar("sched");

#if defined(__GNUC__) || defined(__clang__)
template <class T>
void doNotOptimizeAway(T &&datum)
{
    asm volatile("" : "+r" (datum));
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
    doNotOptimizeAway(ret);
    return ret;
}

int Derived::foo(int i)
{
    return ::foo(i);
}

void init()
{
    registrar.lookup<void(int)>("no return").hook([] (int i) {::foo(i);});
    registrar.lookup<int(int)>("int return").hook([] (int i) {::foo(i);});
}

auto get_runtime_usec() -> decltype(std::chrono::high_resolution_clock::now())
{
    return std::chrono::high_resolution_clock::now();
}

const int NUM_CALLS = 400000000;
std::unordered_map<std::string, std::chrono::duration<double>> times;
std::string baseline;

void run_emptyfor() __attribute__ ((noinline));
void run_emptyfor()
{
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        #ifdef _MSC_VER
        if (static_cast<int>(_getpid()) == 1) {
            std::cout << "pid" << std::endl;
        }
        #else
        asm("");
        #endif
    }
    auto end = get_runtime_usec();
    times["empty for"] = std::chrono::duration<double>(end - start);
}

void run_direct() __attribute__ ((noinline));
void run_direct()
{
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        foo(i);
    }
    auto end = get_runtime_usec();
    times[baseline] = std::chrono::duration<double>(end - start);
}

void run_stdfunction() __attribute__ ((noinline));
void run_stdfunction()
{
    std::function<int(int)> sc = foo;
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        sc(i);
    }
    auto end = get_runtime_usec();
    times["std::function"] = std::chrono::duration<double>(end - start);
}

void run_virtual() __attribute__ ((noinline));
void run_virtual()
{
    auto d = new Derived();
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        d->foo(i);
    }
    auto end = get_runtime_usec();
    times["virtual"] = std::chrono::duration<double>(end - start);
}

void run_function() __attribute__ ((noinline));
void run_function()
{
    Function<int(int)> sc = foo;
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        sc(i);
    }
    auto end = get_runtime_usec();
    times["conduit/function"] = std::chrono::duration<double>(end - start);
}

void run_scwcopy() __attribute__ ((noinline));
void run_scwcopy()
{
    Function<int(int)> sc;
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        sc = foo;
        sc(i);
    }
    auto end = get_runtime_usec();
    times["sc w/ copy"] = std::chrono::duration<double>(end - start);
}

void run_channel() __attribute__ ((noinline));
void run_channel()
{
    #if 0
    {
        auto c = registrar.lookup<int(int)>("int return");
        auto start = get_runtime_usec();
        for (int i = 0; i < NUM_CALLS; ++i) {
            c(i);
        }
        auto end = get_runtime_usec();
        times["return channel"] = std::chrono::duration<double>(end - start);
    }
    #endif
    {
        auto c = registrar.lookup<void(int)>("no return");
        auto start = get_runtime_usec();
        for (int i = 0; i < NUM_CALLS; ++i) {
            c(i);
        }
        auto end = get_runtime_usec();
        times["no return channel"] = std::chrono::duration<double>(end - start);
    }
}

void run_emptychannel() __attribute__ ((noinline));
void run_emptychannel()
{
    auto c = registrar.lookup<int(int)>("empty", "empty");
    auto start = get_runtime_usec();
    for (int i = 0; i < NUM_CALLS; ++i) {
        c(i);
    }
    auto end = get_runtime_usec();
    times["empty channel"] = std::chrono::duration<double>(end - start);
}

uint64_t run_scheduler_test()
{
    baseline = "direct";

    // run_emptyfor();
    // run_direct();
    for (int i = 0; i < NUM_CALLS; ++i)
        run_channel();
    // run_stdfunction();
    // run_virtual();
    // run_function();
    // run_scwcopy();
    // run_emptychannel();

#if 0
    {
        auto c = LOOKUP(registrar, "foo", (int));
        start = get_runtime_usec();
        for (int i = 0; i < NUM_CALLS; ++i) {
            NOW(c, i);
            Scheduler::scheduler().deliver();
            Scheduler::scheduler().next();
        }
        end = get_runtime_usec();
        times["scheduled"] = std::chrono::duration<double>(end - start);
    }

    {
        auto c = LOOKUP(registrar, "foo", (int));
        start = get_runtime_usec();
        for (int i = 0; i < NUM_CALLS; ++i) {
            NOW(c, i);
            NOW(c, i);
            Scheduler::scheduler().deliver();
            Scheduler::scheduler().next();
        }
        end = get_runtime_usec();
        times["scheduled twice"] = std::chrono::duration<double>(end - start);
    }

    {
        auto c = LOOKUP(registrar, "foo", (int));
        start = get_runtime_usec();
        for (int i = 0; i < NUM_CALLS; ++i) {
            NOW(c, i);
            NOW(c, i);
            NOW(c, i);
            Scheduler::scheduler().deliver();
            Scheduler::scheduler().next();
        }
        end = get_runtime_usec();
        times["scheduled thrice"] = std::chrono::duration<double>(end - start);
    }
#endif

    return NUM_CALLS;
}

void print_stats(uint64_t iterations)
{
    auto baseline_time = times[baseline];
    std::map<double, std::string> ordered_by_perf;
    for (auto &p : times) {
        auto percent_of_baseline = 100.0 - (baseline_time / p.second) * 100;
        ordered_by_perf[percent_of_baseline] = p.first;
    }
    for (auto &p_ : ordered_by_perf) {
        auto &p = times[p_.second];
        fmt::print("{:30}{:>10.4f} {:>10.4f}% {:>.4}MHz\n",
                   p_.second,
                   p.count(),
                   ((baseline_time / p) * 100),
                   (static_cast<double>(iterations) / std::chrono::duration_cast<std::chrono::microseconds>(p).count()));
        // std::cout << p.first << "                \t" << p.second.count() << "                   \t" << 
                     // ((baseline_time / p.second) * 100) << "\t" << 
                     // (static_cast<double>(iterations) / p.second.count()) << "MHz\n";
    }
}

void init();
int main()
{
    srand(7);
    init();
    print_stats(run_scheduler_test());

    // times.clear();
    // run_pipeline_test();
    // print_stats();
    std::cout << "foo was called " << foo_calls << " times\n";
}
