
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/conduit.h>
#include <gtest/gtest.h>

using namespace conduit;

struct Foo { std::string s; };
TEST(conduit_changer, basic)
{
    conduit::Registrar reg("dut");

    using sig = void(int, double, Foo);
    auto ci = reg.publish<sig>("test", "dut");

    bool no_arg_called = false;
    reg.subscribe<sig>("test", [&] { no_arg_called = true; });

    int int_arg_called = -1;
    reg.subscribe<sig>("test", [&] (int i) { int_arg_called = i; });

    double double_arg_called = 0;
    reg.subscribe<sig>("test", [&] (double d) { double_arg_called = d; });

    Foo foo_arg_called;
    reg.subscribe<sig>("test", [&] (Foo f) { foo_arg_called.s = f.s; });

    int two_arg_int = -1;
    double two_arg_double = 0;
    reg.subscribe<sig>("test", [&] (int i, double d) { two_arg_int = i; two_arg_double = d; });

    ci(10, 3.14, Foo{"foo arg"});

    ASSERT_EQ(no_arg_called, true);
    ASSERT_EQ(int_arg_called, 10);
    ASSERT_EQ(double_arg_called, 3.14);
    ASSERT_EQ(foo_arg_called.s, "foo arg");
    ASSERT_EQ(two_arg_int, 10);
    ASSERT_EQ(two_arg_double, 3.14);
}
