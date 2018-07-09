
#include <format.h>
#include <conduit/conduit.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <queue>
#include <random>

struct QueueEntry
{
    int priority;
    conduit::SmallCallable<void()> event;

    friend bool operator <(const QueueEntry &lhs, const QueueEntry &rhs)
    {
        return lhs.priority < rhs.priority;
    }
};

int main(int argc, char const *argv[])
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    conduit::Registrar reg("reg", L);

    // This example uses 2 channels to demonstrate how our queue can hold
    // channels of any signature.
    auto int_print = reg.lookup<void(int, int)>("int print channel");
    auto float_print = reg.lookup<void(int, int, float)>("float print channel");

    reg.lookup<void(int, int)>("int print channel").hook([] (int i, int priority) {
        fmt::print("{} was inserted with priority {}\n", i, priority);
    });
    reg.lookup<void(int, int, float)>("float print channel").hook([] (int i, int priority, float value) {
        fmt::print("{} was inserted with priority {} and with float value {}\n", i, priority, value);
    });

    // A priority queue of events. Each QueueEntry has a priority and a function
    // wrapper holding the actual work to perform. The function wrapper can hold
    // anything that can be called without arguments (QueueEntry::event is a
    // nullary type erased function adapter).
    std::priority_queue<QueueEntry> queue;

    // Get our random number generator ready
    std::random_device rd;
    std::mt19937 eng(rd());
    std::uniform_int_distribution<> dist(0, 100);

    // Queue messages on either the int_print or float_print channels.
    // conduit::make_delayed pairs a callable (in this case our channel) with its
    // arguments and saves it for later. See below where we apply the arguments
    // to the channel to send a message on the channel.
    for (int i = 0; i < 10; ++i) {
        auto r = dist(eng);
        if (r & 1) {
            queue.push(QueueEntry{r, conduit::make_delayed(int_print, i, r)});
        } else {
            queue.push(QueueEntry{r, conduit::make_delayed(float_print, i, r, static_cast<float>(i) / r)});
        }
    }

    // Now that we have events in our priority_queue, execute them in priority
    // order.
    while (!queue.empty()) {
        queue.top().event();
        queue.pop();
    }
}
