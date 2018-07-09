
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <random>

void opposite_printer(const std::string &s)
{
    std::cout << ((s == "hello") ? "world" : "hello");
}

int main(int argc, char const *argv[])
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Create a registrar. This represents a single namespace in which to find
    // channels and Registrars are the only way to create a channel. Components
    // in your model that communicate over channels would need to be passed the
    // same Registrar to find the required channels.
    conduit::Registrar reg("reg", L);

    // Lookup the "print channel". This provides a handle that can be used to
    // send messages on the channel.
    auto print = LOOKUP(reg, "print channel", (std::string));

    // Hook (subscribe, listen, etc.) on the print channel. We hook it twice
    // here to demonstrate any callable can be used to receive messages on a
    // channel (as long as it meets the requirements of the channel's function
    // signature).
    HOOK(reg, "print channel", (std::string))([] (const std::string &s) {
        std::cout << s << ", ";
    });
    HOOK(reg, "print channel", (std::string))(opposite_printer);

    // generate a random string of 0s and 1s.
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(0, 1);
    std::vector<int> values(10);
    std::generate(values.begin(), values.end(), [&dist, &eng] () mutable {
        return dist(eng);
    });

    // conduit::debug() = true;

    // send a message on the print channel based on our random string of 0s and
    // 1s. Messages are sent on channels using the function call syntax.
    std::for_each(values.begin(), values.end(), [print] (int value) {
        if (value) {
            print("hello");
        } else {
            print("world");
        }
        std::cout << "\n";
    });
}

