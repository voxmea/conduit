
#ifndef EXAMPLES_PIPELINE_H_
#define EXAMPLES_PIPELINE_H_

#include <conduit/conduit.h>
#include <conduit/function.h>
#include <vector>
#include <functional>
#include <string>
#include <stack>
#include <string>
#include <unordered_map>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <pybind11/pybind11.h>

namespace pe
{

struct SimInit
{
    struct Init
    {
        std::string name;
        conduit::Function<void(conduit::Registrar &)> init;
    };

    template <typename T>
    SimInit(T &&t, std::string name)
    {
        initializers().emplace_back(Init{name, std::forward<T>(t)});
    }

    static std::vector<Init> &initializers()
    {
        static std::vector<Init> v;
        return v;
    }
};

struct Instr
{
    std::string op;
    conduit::Function<void(std::stack<int> &)> exec;
};

inline void print_arg(std::ostream &stream, const Instr &i)
{
    fmt::print(stream, "op:'{}'", i.op);
}

inline Instr instr_lookup(const std::string &op, const std::string &arg)
{
    static const std::unordered_map<std::string, conduit::Function<Instr(const std::string &arg)>> instructions = {
        {"push", [] (const std::string &arg) {
            return Instr{"push", [arg] (std::stack<int> &stack) {stack.push(std::stoi(arg));}};
        }},
        {"add", [] (const std::string &arg) {
            return Instr{"add", [arg] (std::stack<int> &stack) {
                auto i = stack.top();
                stack.pop();
                stack.push(i + std::stoi(arg));
            }};
        }},
        {"sub", [] (const std::string &arg) {
            return Instr{"sub", [arg] (std::stack<int> &stack) {
                auto i = stack.top();
                stack.pop();
                stack.push(i - std::stoi(arg));
            }};
        }},
        {"print", [] (const std::string &arg) {
            return Instr{"print", [arg] (std::stack<int> &stack) {fmt::print(fmt::color::red, "{} {}\n", arg, stack.top());}};
        }},
        {"exit", [] (const std::string &) {
            return Instr{"exit", [] (std::stack<int> &) {}};
        }}
    };
    auto iter = instructions.find(op);
    BOTCH(iter == instructions.end(), "unknown op {}\n", op);
    return iter->second(arg);
}

}

PYBIND11_MAKE_OPAQUE(std::stack<int>);

#endif
