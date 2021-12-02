//
// g++ conduit-example.cpp -std=c++14 -Iconduit/include -DCONDUIT_NO_LUA -DCONDUIT_NO_PYTHON
//
#include <cstdio>
#include <iostream>
#include "conduit/conduit.h"
using namespace conduit;

class first {
public:
    Registrar &reg;
    first(Registrar &_reg) : reg(_reg) {
    }
    conduit_run(reg.subscribe<void(int)>("int_channel", [this](int input) {
        std::cout << "First Received " << input << std::endl;
    }));
};

class second {
public:
    Registrar &reg;
    second(Registrar &_reg) : reg(_reg) {
        reg.subscribe<void(int)>("int_channel", second::sub);
    }
    conduit_run(reg.subscribe<void(int)>("int_channel", [this](int input) {
        std::cout << "Second Lambda Received " << input << std::endl;
    }));
    static void sub(int input) {
        std::cout << "Second Sub Received " << input << std::endl;
    }
};


class third {
public:
    Registrar &reg;
    third(Registrar &_reg) : reg(_reg) {
    }
    ChannelInterface<void(int)> pub = reg.publish<void(int)>("int_channel");
    conduit_run(reg.subscribe<void(int)>("int_channel", [this](int input) {
        std::cout << "Third Received " << input << std::endl;
    }));
};

class pub_sub {
public:
    Registrar reg{"model"};
    first f;
    second s;
    third t;
    pub_sub() : f(reg), s(reg), t(reg) {

        ChannelInterface<void(int)> pub_local_func = reg.publish<void(int)>("int_channel");

        std::cout << "Executing First Pub:" << std::endl;
        pub_local_func(9);
        std::cout << "Executing Second Pub:" << std::endl;
        t.pub(25);
    }
    ChannelInterface<void(int)> pub_sub_pub = reg.publish<void(int)>("int_channel");
};

int main(int argc, char **argv) {

    pub_sub p;
    std::cout << "Executing Third pub:" << std::endl;
    p.pub_sub_pub(35);

}
