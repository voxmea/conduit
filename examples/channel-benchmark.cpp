
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/conduit.h>
#include <random>
#include <vector>

using namespace conduit;

#include <benchmark/benchmark.h>

std::mt19937 gen(7);
std::uniform_int_distribution<int> dist(1.0, 10.0);

static void random_gen(benchmark::State &state)
{
    for (auto _ : state) {
        benchmark::DoNotOptimize(dist(gen));
        benchmark::DoNotOptimize(dist(gen));
    }
}
BENCHMARK(random_gen);

static void no_subscriber(benchmark::State &state)
{
    Registrar reg("dut");
    auto ci = reg.publish<void(int, int)>("test", "dut");
    for (auto _ : state) {
        ci(dist(gen), dist(gen));
    }
}
BENCHMARK(no_subscriber);

static void one_subscriber(benchmark::State &state)
{
    Registrar reg("dut");
    reg.subscribe<void(int, int)>("test", [] (int i, int j) {
        benchmark::DoNotOptimize(i);
        benchmark::DoNotOptimize(j);
    }, "dut");
    auto ci = reg.publish<void(int, int)>("test", "dut");
    for (auto _ : state) {
        ci(dist(gen), dist(gen));
    }
}
BENCHMARK(one_subscriber);

static void modified_subscriber(benchmark::State &state)
{
    Registrar reg("dut");
    reg.subscribe<void(int, int)>("test", [] () {}, "dut");
    auto ci = reg.publish<void(int, int)>("test", "dut");
    for (auto _ : state) {
        ci(dist(gen), dist(gen));
    }
}
BENCHMARK(modified_subscriber);

static void view_unmodified_subscriber(benchmark::State &state)
{
    Registrar reg("dut");
    reg.subscribe("test", [] (int i, int j) {
        benchmark::DoNotOptimize(i);
        benchmark::DoNotOptimize(j);
    }, "dut");
    auto ci = reg.publish<void(int, int)>("test", "dut");
    for (auto _ : state) {
        ci(dist(gen), dist(gen));
    }
}
BENCHMARK(view_unmodified_subscriber);

static void __attribute__ ((noinline)) noinline_func(int i, int j) {
    benchmark::DoNotOptimize(i);
    benchmark::DoNotOptimize(j);
}
static void free_function(benchmark::State &state)
{
    for (auto _ : state) {
        noinline_func(dist(gen), dist(gen));
    }
}
BENCHMARK(free_function);

struct Base { virtual ~Base() {} virtual void test(int, int) = 0; };
struct Virtual : Base
{
    void test(int i, int j) override
    {
        benchmark::DoNotOptimize(i);
        benchmark::DoNotOptimize(j);
    }
};
static void virtual_function(benchmark::State &state)
{
    auto v = new Virtual();
    for (auto _ : state) {
        v->test(dist(gen), dist(gen));
    }
    delete v;
}
BENCHMARK(virtual_function);

static void large_heap(benchmark::State &state)
{
    std::vector<int> vec(1024*1024, 1);
    Registrar reg("dut");
    reg.subscribe<void(std::vector<int>)>("test", [] (const std::vector<int> &vec) {
        benchmark::DoNotOptimize(vec.data());
    }, "dut");
    auto ci = reg.publish<void(std::vector<int>)>("test", "dut");
    for (auto _ : state) {
        ci(vec);
    }
}
BENCHMARK(large_heap);

BENCHMARK_MAIN();
