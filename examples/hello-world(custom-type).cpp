#include <fmt/format.h>
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <random>

struct CustomType
{
    int data = 0;

    CustomType() = default;
    CustomType(int d_) : data(d_) {}
    operator int() const {return data;}

// enable this code to customize printing of CustomType through conduit.
#if 0
    friend void print_arg(std::ostream &stream, const CustomType &ct)
    {
        fmt::print(stream, "ct-{}", ct.data);
    }
#endif
};

void opposite_printer(int value)
{
    fmt::print("{}", !value);
}

int main(int argc, char const *argv[])
{
    // Create a registrar. This represents a single namespace in which to find
    // channels and Registrars are the only way to create a channel. Components
    // in your model that communicate over channels would need to be passed the
    // same Registrar to find the required channels.
    conduit::Registrar reg("reg", nullptr);

    // Lookup the "print channel". This provides a handle that can be used to
    // send messages on the channel.
    auto print = reg.lookup<void(CustomType)>("print channel", "printer");

    // Hook (subscribe, listen, etc.) on the print channel. We hook it twice
    // here to demonstrate any callable can be used to receive messages on a
    // channel (as long as it meets the requirements of the channel's function
    // signature).
    reg.lookup<void(CustomType)>("print channel").hook([] (int i) {
        fmt::print("{} ", i);
    });
    reg.lookup<void(CustomType)>("print channel").hook(opposite_printer);

    // generate a random string of 0s and 1s.
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(0, 1);
    std::vector<int> values(10);
    std::generate(values.begin(), values.end(), [&dist, &eng] () mutable {
        return dist(eng);
    });

    reg.set_debug(true);

    // send a message on the print channel based on our random string of 0s and
    // 1s. Messages are sent on channels using the function call syntax.
    std::for_each(values.begin(), values.end(), [print] (int value) {
        print(value);
        fmt::print("\n");
    });
}
