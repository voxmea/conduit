
#include <chrono>
#include <iostream>
#include <random>
#include <cstdint>
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#include <conduit/conduit.h>

int main(int argc, char const *argv[])
{
    conduit::Registrar reg("reg");

    auto sig = reg.lookup<void(int)>("signal");

    uint64_t total = 0;
    reg.lookup<void(int)>("signal").hook([&] (int i) {total += i;});
    reg.lookup<void(int)>("signal").hook([&] (int i) {total += i;});

    std::mt19937 eng(7);
    std::uniform_int_distribution<> dist(0, 1);
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i)
        sig(dist(eng));
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << total << " took " << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() << std::endl; 
    return 0;
}
