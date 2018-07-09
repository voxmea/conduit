
#include <iostream>
#include <conduit/conduit.h>
#include <conduit/binder.h>

using namespace conduit;

ChannelInterface<void(int)> unique_name;
template <typename T>
void foo_template(T t)
{
    std::cout << __FUNCTION__ << std::endl;
    unique_name(t);
}

void foo(int i)
{
    std::cout << __FUNCTION__ << std::endl;
    unique_name(i);
}

struct Foo {
    Registrar &reg;
    Foo(Registrar &r_) : reg(r_) {}

    ChannelInterface<void(int)> ci = reg.lookup<void(int)>("void(int)-next");

    void foo(int i) const
    {
        std::cout << __FUNCTION__ << std::endl;
        ci(i);
    }
    std::string foo_id = reg.hook<void(int)>("void(int)", make_binder(this, &Foo::foo));

    template <typename T>
    void bar(T i) const
    {
        std::cout << __FUNCTION__ << std::endl;
        ci(i);
    }
    std::string bar_id = reg.hook<void(int)>("void(int)", make_binder(this, &Foo::bar<int>));
};

int main(int argc, char *argv[])
{
    Registrar reg("test", nullptr);

    unique_name = reg.lookup<void(int)>("void(int)-next");
    auto ci = reg.lookup<void(int)>("void(int)-next");

    reg.hook<void(int)>("void(int)", [flibet = ci] (auto i) {
        std::cout << __FUNCTION__ << std::endl;
        flibet(i);
    });
    reg.hook<void(int)>("void(int)", [ci] (int i) {
        std::cout << __FUNCTION__ << std::endl;
        ci(i);
    });

    Foo f(reg);

    reg.hook<void(int)>("void(int)", foo);
    reg.hook<void(int)>("void(int)", foo_template<int>);

    reg.lookup<void(int)>("void(int)")(argc);
}
