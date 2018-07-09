

#define CONDUIT_NO_LUA
#include <conduit/conduit.h>
#include <conduit/botch.h>
#include <iostream>

int main(int argc, char const *argv[])
{
    try {
        BOTCH(true, "whatever");
    } catch (const conduit::ConduitError &ex) {
        std::cerr << ex.what() << std::endl;
    }
}
