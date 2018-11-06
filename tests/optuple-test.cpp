
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/conduit.h>
#include <gtest/gtest.h>

using namespace conduit;

struct NoCopy
{
    NoCopy() = default;
    NoCopy(const NoCopy &) = delete;
    NoCopy &operator =(const NoCopy &) = delete;
};

TEST(Optuple, basic)
{
    conduit::Registrar reg("test");
    int left = 0, right = 0;
    auto c = [&] (int i, int j) {
        left = i, right = j;
    };
    merge(c, reg.publish<void(int)>("one"), reg.publish<void(int)>("two"));
    reg.publish<void(int)>("one")(10);
    reg.publish<void(int)>("two")(20);
    ASSERT_EQ(left, 10);
    ASSERT_EQ(right, 20);
}

TEST(Optuple, basic_response)
{
    conduit::Registrar reg("test");
    int left = 0, right = 0;
    bool resp = false;
    auto c = [&] (int i, int j) {
        left = i, right = j;
    };
    auto r = [&] { resp = true; };
    auto optuple = merge(c,
                         "merge",
                         r,
                         reg.publish<void(int)>("one"),
                         reg.publish<void(int)>("two"));
    reg.publish<void(int)>("one")(10);
    reg.publish<void(int)>("two")(20);
    ASSERT_EQ(left, 10);
    ASSERT_EQ(right, 20);
    ASSERT_EQ(resp, true);
}

#if 0
int main(int argc, char const *argv[])
{
    auto response = [] {
        fmt::print("response\n");
    };

    // sanity
    {
    }

    // one
    {
        conduit::Registrar reg("test");
        auto c = [] {
            fmt::print("empty\n");
        };
        auto optuple = merge(c, reg.find<void()>("one"));
        reg.find<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("single 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c, reg.find<void(int, int)>("one"));
        reg.find<void(int, int)>("one")(1, 2);
    }

    // two
    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("double 1 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.find<void()>("one"),
                             reg.find<void(int)>("two"));
        reg.find<void()>("one")();
        optuple->reset();
        reg.find<void(int)>("two")(1);
        reg.find<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("double 1 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.find<void(int)>("one"),
                             reg.find<void()>("two"));
        reg.find<void(int)>("one")(1);
        optuple->reset();
        reg.find<void()>("two")();
        reg.find<void(int)>("one")(1);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("double 1 1 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             response,
                             reg.find<void(int)>("one"),
                             reg.find<void(int)>("two"));
        optuple->reset();
        reg.find<void(int)>("one")(1);
        reg.find<void(int)>("two")(1);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] () {
            fmt::print("double empty\n");
        };
        auto optuple = merge(c,
                             response,
                             reg.find<void()>("one"),
                             reg.find<void()>("two"));
        optuple->reset();
        reg.find<void()>("one")();
        reg.find<void()>("two")();
    }

    // three
    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.find<void()>("one"),
                             reg.find<void(int)>("two"),
                             reg.find<void(int)>("three"));
        reg.find<void()>("one")();
        reg.find<void(int)>("two")(1);
        reg.find<void(int)>("three")(2);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.find<void(int)>("two"),
                             reg.find<void()>("one"),
                             reg.find<void(int)>("three"));
        reg.find<void(int)>("three")(2);
        reg.find<void()>("one")();
        reg.find<void(int)>("two")(1);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.find<void(int)>("two"),
                             reg.find<void(int)>("three"),
                             reg.find<void()>("one"));
        reg.find<void(int)>("two")(1);
        reg.find<void(int)>("three")(2);
        reg.find<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i, int j) {
            fmt::print("three 1 2 i {} j {}\n", i, j);
        };
        auto optuple = merge(c,
                             reg.find<void(int)>("two"),
                             reg.find<void(int)>("three"),
                             reg.find<void()>("one"));
        reg.find<void(int)>("two")(1);
        reg.find<void(int)>("three")(2);
        reg.find<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("three 2 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.find<void()>("two"),
                             reg.find<void(int)>("three"),
                             reg.find<void()>("one"));
        reg.find<void()>("two")();
        reg.find<void(int)>("three")(2);
        reg.find<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("three 2 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.find<void(int)>("three"),
                             reg.find<void()>("two"),
                             reg.find<void()>("one"));
        reg.find<void(int)>("three")(2);
        reg.find<void()>("two")();
        reg.find<void()>("one")();
    }

    {
        conduit::Registrar reg("test");
        auto c = [] (int i) {
            fmt::print("three 2 i {}\n", i);
        };
        auto optuple = merge(c,
                             reg.find<void()>("two"),
                             reg.find<void()>("one"),
                             reg.find<void(int)>("three"));
        reg.find<void()>("two")();
        reg.find<void()>("one")();
        reg.find<void(int)>("three")(2);
    }

    {
        conduit::Registrar reg("test");
        auto c = [] () {
            fmt::print("three empty\n");
        };
        auto optuple = merge(c,
                             reg.find<void()>("two"),
                             reg.find<void()>("one"),
                             reg.find<void()>("three"));
        reg.find<void()>("two")();
        reg.find<void()>("one")();
        reg.find<void()>("three")();
    }

    // noisy
    {
        conduit::Registrar reg("test");
        auto c = [] (Noisy) {
            fmt::print("noisy\n");
        };
        auto optuple = merge(c, reg.find<void(Noisy)>("one"));
        reg.find<void(Noisy)>("one")(Noisy());
    }

    // using channels for everything
    {
        conduit::Registrar reg("test");
        reg.find<void(int, int)>("forward").hook([] (int, int) {
            fmt::print("forward\n");
        });
        reg.find<void()>("token").hook([] {
            fmt::print("token\n");
        });
        auto optuple = merge(reg.find<void(int, int)>("forward"),
                             conduit::Function<void()>(reg.find<void()>("token")),
                             reg.find<void(int)>("one"),
                             reg.find<void(int)>("two"));
        reg.set_debug(true);
        reg.find<void(int)>("one")(1);
        reg.find<void(int)>("two")(2);
    }

    #if 0
    // nocopy
    {
        conduit::Registrar reg("test");
        auto c = [] (NoCopy) {
            fmt::print("nocopy\n");
        };
        auto optuple = merge(c, reg.find<void(NoCopy)>("one"));
        reg.find<void(NoCopy)>("one")(NoCopy());
    }
    #endif

    return 0;
}
#endif
