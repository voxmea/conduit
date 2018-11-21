
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/conduit.h>
#include <gtest/gtest.h>
#include <string>
#include <algorithm>
#include <locale>

using namespace conduit;

TEST(conduit_observer, basics)
{
    int observed_int = 0;
    Observable<int> ob{[&observed_int] (int i) { observed_int = i; }};

    ob.write() = 10;
    ASSERT_EQ(observed_int, 10);

    {
        auto writer = ob.write();
        writer = 11;
        ASSERT_EQ(observed_int, 10);
    }
    ASSERT_EQ(observed_int, 11);
}

TEST(conduit_observer, channel_basics)
{
    Registrar reg("dut");
    Observable<int> ob{reg, "int_observable", "dut"};

    int observed_int = 0;
    reg.subscribe("int_observable", [&] (int i) {
        observed_int = i;
    }, "obs");

    *ob = 10;
    ASSERT_EQ(observed_int, 10);
    ASSERT_EQ(ob.read(), 10);
    ASSERT_EQ(*ob, 10);

    {
        auto writer = *ob;
        writer = 11;
        ASSERT_EQ(observed_int, 10);
        ASSERT_EQ(ob.read(), 11);
    }
    ASSERT_EQ(observed_int, 11);
    ASSERT_EQ(ob.read(), 11);
}

struct NoDefaultInit
{
    int i;
    NoDefaultInit(int i_) : i(i_) {}

    friend bool operator ==(const NoDefaultInit &ndi, int i)
    {
        return ndi.i == i;
    }
};
TEST(conduit_observer, no_default_init)
{
    int observed_int = 0;
    Observable<NoDefaultInit> ob{[&observed_int] (auto ndi) { observed_int = ndi.i; }, -1};
    ASSERT_EQ(ob.read(), -1);

    ob.write() = 10;
    ASSERT_EQ(ob.read(), 10);
    ASSERT_EQ(observed_int, 10);
}

TEST(conduit_observer, increment_operator)
{
    bool observed_increment = false;
    Observable<int> int_ob{[&observed_increment] (int) { observed_increment = true; }, 0};
    ASSERT_EQ(*int_ob, 0);
    auto pre_inc = ++*int_ob;
    ASSERT_EQ(*int_ob, 1);
    ASSERT_EQ(observed_increment, true);
    ASSERT_EQ(pre_inc, 1);
    ++pre_inc;
    ASSERT_EQ(*int_ob, 1);
}

TEST(conduit_observer, subscript_operator)
{
    int observed_push_back = 0;
    Observable<std::vector<int>> vec_ob{[&observed_push_back] (auto &vec) { if (vec.size()) observed_push_back = vec[0]; }};
    ASSERT_EQ(vec_ob->size(), 0);
    vec_ob->push_back(10);
    ASSERT_EQ(vec_ob.read()[0], 10);
    ASSERT_EQ(vec_ob.read().size(), 1);
    ASSERT_EQ(observed_push_back, 10);

    for (auto &i : vec_ob) {
        ASSERT_EQ(i, 10);
    }
    ASSERT_EQ(observed_push_back, 10);
    for (auto &i : *vec_ob) {
        ASSERT_EQ(i, 10);
        i = 20;
        ASSERT_EQ(vec_ob.read()[0], 20);
    }
    ASSERT_EQ(observed_push_back, 20);
}

struct Callable
{
    void operator ()(int &i) { i = 20; }
    int operator ()() { return 10; }
    std::string operator()(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
};
TEST(conduit_observer, call_operator)
{
    Observable<Callable> call_ob{[] (auto &callable) { }};
    auto s = (*call_ob)("QWER");
    ASSERT_EQ(s, "qwer");
    int i = 10;
    (*call_ob)(i);
    ASSERT_EQ(i, 20);
    ASSERT_EQ((*call_ob)(), 10);
}
