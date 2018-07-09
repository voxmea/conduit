
#include <conduit/conduit.h>

using namespace conduit;

int main(int argc, char const *argv[])
{
    Registrar reg("reg");

    auto channel = reg.lookup<void()>("channel");
    channel.hook([] {std::cout << "group 1\n";}, "1", 1);
    channel.hook([] {std::cout << "group 0\n";}, "0", 0);
    channel.hook([] {std::cout << "group -1\n"; return 2;}, "-1", -1);
    channel.hook([] {std::cout << "group 0\n";}, "0.1", 0);
    channel();

    for (auto s : channel.callbacks()) {
        std::cout << s << std::endl;
    }
    return 0;
}
