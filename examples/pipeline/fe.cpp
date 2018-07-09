
#include <format.h>
#include "pipeline.h"
#include "events.h"
#include <memory>
#include <string>
#include <sstream>

using namespace conduit;
using std::string;
using std::istringstream;

namespace
{

struct FE
{
    Registrar &reg;
    FE(Registrar &reg_) : reg(reg_) {}

    uint64_t age = 0;
    size_t offset = 0;
    string input;
    string line;
    istringstream stream;

    string init_id = reg.lookup<void(string)>("start simulation").hook([this] (string input_) {
        input = std::move(input_);
        stream = istringstream(input);
        schedule_data();
    }, "fe");

    string exec_id = reg.lookup<void()>("fetch").hook([this] {
        schedule_data();
    }, "fe");

    ChannelInterface<void(string)> decode = reg.lookup<void(string)>("decode", "fe");
    ChannelInterface<void()> end_sim = reg.lookup<void()>("end simulation", "fe");
    ChannelInterface<void()> schedule_data = reg.lookup<void()>("-FE.schedule_data", "fe");
    string data_id = ev::unique(
        [this] {
            std::getline(stream, line);
            if (!stream) {
                fmt::print("stream failed\n");
                end_sim();
                return;
            }
            ev::next(decode, line);
        },
        {schedule_data}, "fe");
};

pe::SimInit init{[] (Registrar &reg) {
    auto fe = std::make_shared<FE>(reg);
    reg.lookup<void()>("storage").hook([fe] {});
}, "fe"};

}
