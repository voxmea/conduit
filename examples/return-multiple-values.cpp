
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iostream>
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#include <conduit/conduit.h>

namespace std {
template <typename C>
std::ostream &operator <<(std::ostream &stream, const std::vector<C> &vec)
{
    bool first = true;
    fmt::print(stream, "{{");
    for (auto &t : vec) {
        fmt::print(stream, "{}{}", first ? "" : ", ", t);
        first = false;
    }
    fmt::print(stream, "}}");
    return stream;
}
}

void foo(int)
{
    std::cout << "from foo\n";
}

struct Foo
{
    int operator() (int i)
    {
        return i * 20;
    }
};


int my_callback(int i, std::string s)
{
  fmt::print("{} {}\n", i, s);
  return 12;
}


int main(int argc, char const *argv[])
{
    conduit::Registrar reg("reg", nullptr);

    auto sig = reg.lookup<int(int)>("sig");



    {
      // Lots of different ways to hook a single conduit. Each hook has its own return value.
      int mul = 10;
      sig.hook([mul] (int i) {return mul * i;});
      sig.hook([] (int i) {return std::string("asdf");});
      sig.hook([] (int i) {return 3.0;});
      sig.hook([] (int i) -> conduit::Optional<int> {return conduit::OptionalNull();});
      sig.hook([] (int i) -> conduit::Optional<int> {return i;});
      sig.hook(foo);
      sig.hook(Foo());
      
      // Each hook's return values are concatonated here.
      auto multiple_return_values = sig(2);

      // Generic way to print multiple return values
      fmt::print("{}\n", multiple_return_values);    // Prints "{20, {{empty}}, 3, {{empty}}, 2, {{empty}}, 40}"
    }


    fmt::print("---------------------\n");


    {
        // Helper function: waits until each conduit has a message. Then it calls lambda to process them all-at-once.
        auto one = reg.lookup<int(int)>("one");
        auto two = reg.lookup<std::string(std::string)>("two");

	// Either of these do the same thing
#if 1
        conduit::merge([] (int i, std::string s) {
            fmt::print("{} {}\n", i, s);
            return 12;
        }, one, two);
#else
	conduit::merge(my_callback, one, two);
#endif

        one(1);
        two("2");      // Prints "1 2"
    }

    return 0;
}
