
#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/fixvec.h>
#include <gtest/gtest.h>

using namespace conduit;

TEST(fixvec, basic)
{
    FixVec<int, 2> fv;
    {
        auto &elem = fv.push_back(1);
        ASSERT_EQ(fv.front(), 1);
        ASSERT_EQ(fv.back(), 1);
        ASSERT_EQ(elem, 1);
        elem = 2;
        ASSERT_EQ(fv.front(), 2);
        ASSERT_EQ(fv.back(), 2);
    }
    {
        auto &elem = fv.emplace_back(10);
        ASSERT_EQ(fv.front(), 2);
        ASSERT_EQ(fv.back(), 10);
        ASSERT_EQ(elem, 10);
        elem = 2;
        ASSERT_EQ(fv.back(), 2);
    }
}
