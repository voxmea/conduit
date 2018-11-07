
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

TEST(conduit_changer, basic_view)
{
    conduit::Registrar reg("dut");

    using sig = void(int, double, Foo);

    auto ci = reg.publish<sig>("test");
    reg.register_view<void(int)>(ci, ci.name());
    reg.register_view<void(double)>(ci, ci.name());

    int view_int = 0;
    reg.subscribe("test", [&] (int i) {
        view_int = i;
    }, "test");

    double view_double = 0;
    reg.subscribe("test", [&] (double d) {
        view_double = d;
    }, "test");

    {
        bool botched = false;
        try {
            reg.subscribe("test", [&] (Foo) { }, "test");
        } catch (const conduit::ConduitError &) {
            botched = true;
        }
        ASSERT_TRUE(botched);
    }

    reg.register_view<void(Foo)>(ci, ci.name());
    {
        bool botched = false;
        try {
            reg.subscribe("test", [&] (Foo) { }, "test");
        } catch (const conduit::ConduitError &) {
            botched = true;
        }
        ASSERT_FALSE(botched);
    }

    ci(10, 3.14, Foo{"foo arg"});
    ASSERT_EQ(view_int, 10);
    ASSERT_EQ(view_double, 3.14);
}

TEST(conduit_changer, transformation_view)
{
    conduit::Registrar reg("dut");

    using sig = void(int, double);

    auto ci = reg.publish<sig>("test");
    reg.register_view<void(std::string)>(ci, [] (double d) {
        return std::to_string(d) + " extra test val";
    }, ci.name());

    std::string trans_str;
    reg.subscribe("test", [&] (const std::string &s) {
        trans_str = s;
    }, "test");

    ci(10, 3.14);
    ASSERT_EQ(trans_str, (std::to_string(3.14) + " extra test val"));
}

TEST(conduit_changer, tuple_transformation_view)
{
    conduit::Registrar reg("dut");

    using sig = void(int, double);

    auto ci = reg.publish<sig>("test");
    reg.register_view<void(std::string, std::string)>(ci, [] (int i, double d) {
        return std::make_tuple(std::to_string(i), std::to_string(d));
    }, ci.name());

    std::string trans_str;
    reg.subscribe("test", [&] (const std::string &l, const std::string &r) {
        trans_str = l + r;
    }, "test");

    ci(10, 3.14);
    ASSERT_EQ(trans_str, (std::to_string(10) + std::to_string(3.14)));
}
