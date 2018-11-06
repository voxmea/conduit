

#define CONDUIT_NO_LUA
#define CONDUIT_NO_PYTHON
#define CONDUIT_CONDUIT_SOURCE_STRING_INTERNING
#include <conduit/conduit.h>
#include <conduit/botch.h>
#include <gtest/gtest.h>

TEST(conduit, BOTCH_throw_test)
{
    std::string throw_string;
    try {
        BOTCH(true, "whatever");
    } catch (const conduit::ConduitError &ex) {
        throw_string = ex.what();
    }
    ASSERT_TRUE(throw_string.size());
}
