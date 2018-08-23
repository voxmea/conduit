

#include <fmt/format.h>
#include <pipeline.h>
#include <events.h>
#include <memory>
#include <string>
#include <unordered_map>

using namespace conduit;
using namespace pe;

namespace
{

struct Exec
{
    Registrar &reg;
    Exec(Registrar &reg_) : reg(reg_) {}

    std::stack<int> stack;
    ChannelInterface<void()> fetch = reg.lookup<void()>("fetch", "exec");
    ChannelInterface<void()> sim_exit = reg.lookup<void()>("end simulation", "exec");
    std::string exec_id = reg.lookup<void(Instr)>("exec").hook([this] (const Instr &instr) {
        if (instr.op == "exit")
            sim_exit();
        instr.exec(stack);
        ev::sched(1, fetch);
    }, "exec");
};

SimInit init{[] (Registrar &reg) {
    auto exec = std::make_shared<Exec>(reg);
    reg.lookup<void()>("storage").hook([exec] {});

    pybind11::module m = pybind11::reinterpret_borrow<pybind11::module>(PyImport_AddModule("conduit"));
    pybind11::class_<Instr>(m, "Instr")
        .def(pybind11::init<>())
        .def_readwrite("op", &Instr::op)
        .def_readwrite("exec", &Instr::exec);
    pybind11::class_<std::stack<int>>(m, "Set")
        .def(pybind11::init<>());
    using Func = conduit::Function<void(std::stack<int> &)>;
    pybind11::class_<Func>(m, "Function")
        .def(pybind11::init<>())
        .def(pybind11::init<Func>())
        .def("__call__", [] (const Func &f, std::stack<int> &s) {
            return f(s);
        });
}, "exec"};

}
