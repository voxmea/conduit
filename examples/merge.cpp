#include <fmt/format.h>
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <random>

struct NoDefaultConstruct
{
    NoDefaultConstruct() = delete;
    NoDefaultConstruct(int) {}
    NoDefaultConstruct(const NoDefaultConstruct &) = default;
    // ~NoDefaultConstruct() {fmt::print("destructing NoDefaultConstruct\n");}
};

int main(int argc, char const *argv[])
{
    {
        conduit::Registrar reg("reg", nullptr);

        auto print_1 = reg.lookup<void(std::reference_wrapper<std::string>)>("print 1 channel", "main");
        auto print_2 = reg.lookup<void(std::string)>("print 2 channel", "main");

        auto ptr = conduit::merge([] (std::string one, std::string two) {
            fmt::print("merge received - {} {}\n", one, two);
        }, print_1, print_2);

        std::string s = "asdf";
        print_1(s);
        print_2(s + "qwer");
        print_1(s);
        print_2(s + "zxcv");
    }

    {
        conduit::Registrar reg("reg", nullptr);

        auto one = reg.lookup<void()>("one", "main");
        auto two = reg.lookup<void()>("two", "main");

        one.hook([] {fmt::print("one\n");});
        two.hook([] {fmt::print("two\n");});

        auto ptr = conduit::merge([] {
            fmt::print("merge fired\n");
        }, one, two);

        one();
        two();
    }

    {
        conduit::Registrar reg("reg", nullptr);

        auto one = reg.lookup<void()>("one", "main");

        auto ptr = conduit::merge([] {
            fmt::print("merge fired\n");
        }, one);

        one();
    }

    {
        conduit::Registrar reg("reg", nullptr);

        auto one = reg.lookup<void(NoDefaultConstruct)>("one", "main");

        auto ptr = conduit::merge([] (NoDefaultConstruct) {
            fmt::print("NoDefaultConstruct merge fired\n");
        }, one);

        one(NoDefaultConstruct(1));
    }
}
