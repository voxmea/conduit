
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/fixvec.h>
#include <conduit/accordion.h>
#include <random>
#include <vector>
#include <deque>

using namespace conduit;

#include <benchmark/benchmark.h>

std::mt19937 gen(7);
std::uniform_int_distribution<int> dist(1.0, 10.0);

using SchedulerCycle = conduit::Accordion<conduit::FixVec<int, 1000>>;
using SchedulerQueue = conduit::Accordion<SchedulerCycle>;

static void random_gen(benchmark::State &state)
{
    for (auto _ : state) {
        benchmark::DoNotOptimize(dist(gen));
    }
}
BENCHMARK(random_gen);

static void simple_scheduling(benchmark::State &state)
{
    Accordion<int> accordion;
    for (auto _ : state) {
        accordion[accordion.now() + 1] = 1;
        benchmark::DoNotOptimize(accordion.front());
        accordion.pop_front();
    }
}
BENCHMARK(simple_scheduling);

static void deque_scheduling(benchmark::State &state)
{
    std::deque<int> deque;
    for (auto _ : state) {
        deque.push_front(1);
        benchmark::DoNotOptimize(deque.front());
        deque.pop_front();
    }
}
BENCHMARK(deque_scheduling);

static void random_scheduling(benchmark::State &state)
{
    
}

BENCHMARK_MAIN();
