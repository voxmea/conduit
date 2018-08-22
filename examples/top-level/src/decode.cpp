
#include <fmt/format.h>
#include <pipeline.h>
#include <events.h>
#include <memory>
#include <string>
#include <unordered_map>

using namespace conduit;
using namespace pe;
using std::string;

namespace
{

std::vector<std::string> split(const std::string &s, const std::string &delim = " \n\t\r")
{
   std::vector<std::string> ret;
   for (auto begin = s.begin(); begin != s.end();) {
       auto pos = std::find_first_of(begin, s.end(), delim.begin(), delim.end());
       if (begin != pos)
           ret.emplace_back(begin, pos);
       if (pos == s.end())
           break;
       begin = std::next(pos);
   }
   return ret;
}

struct Decode
{
    Registrar &reg;
    Decode(Registrar &reg_) : reg(reg_) {}

    ChannelInterface<void()> fetch = reg.lookup<void()>("fetch", "decode");
    ChannelInterface<void(Instr)> exec = reg.lookup<void(Instr)>("exec", "decode");
    string decode_id = reg.lookup<void(string)>("decode").hook([this] (string line) {
        auto parts = split(line);
        if (parts.size() != 2) {
            fetch();
            return;
        }
        ev::sched(1, exec, pe::instr_lookup(parts[0], parts[1]));
    }, "decode");
};

#if 1
SimInit init{[] (Registrar &reg) {
    auto decode = std::make_shared<Decode>(reg);
    reg.lookup<void()>("storage").hook([decode] {});
}, "decode"};
#endif

}
