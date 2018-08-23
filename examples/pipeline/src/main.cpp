#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG 1
#else
#include <Python.h>
#endif

#include <unordered_map>
#include <fstream>

#include <conduit/conduit.h>

#include <stdlib.h>
#ifdef _MSC_VER
#include <editline/readline.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include <pybind11/eval.h>
#include <pybind11/iostream.h>

#include <fmt/format.h>

#include <events.h>
#include <pipeline.h>
#include <pipeline-version.h>
#include <util.h>

// TODO: fix registrar lifetimes
static std::vector<std::unique_ptr<conduit::Registrar>> registrars;
void set_channel_debug(bool debug_value)
{
    for (auto &reg_ptr : registrars) {
        reg_ptr->visit([debug_value] (auto &reb) {
            const auto &name = reb.name();
            if (name.empty())
                return;
            if (name[0] == '-')
                return;
            if (name == "pre_cycle" || name == "post_cycle")
                return;
            reb.set_debug(debug_value);
        });
    }
}

void init()
{
    // conduit object must exist
    pybind11::module m = pybind11::module::import("__main__");
    pybind11::module conduit = pybind11::reinterpret_borrow<pybind11::module>(PyImport_AddModule("conduit"));
    if (!hasattr(m, "conduit")) {
        setattr(m, "conduit", conduit);
    }
    if (!hasattr(conduit, "registrars")) {
        pybind11::dict reg;
        setattr(conduit, "registrars", reg);
    }
    conduit.def("create_reg", [] (std::string name) {
        auto sp = std::make_unique<conduit::Registrar>(name);
        registrars.emplace_back(std::move(sp));
        return pybind11::eval<>(fmt::format("conduit.registrars['{}']\n", name));
    });
    conduit.def("alias", [] (pybind11::object dst_, pybind11::object src_, std::string name) {
        auto &src = *static_cast<conduit::Registrar *>(pybind11::reinterpret_borrow<pybind11::capsule>(src_.attr("ptr")));
        auto &dst = *static_cast<conduit::Registrar *>(pybind11::reinterpret_borrow<pybind11::capsule>(dst_.attr("ptr")));
        dst.alias(src, name);
    });
    conduit.def("set_channel_debug", [] (bool debug_value) {
        set_channel_debug(debug_value);
    });

    // set BOTCH to start Python
    conduit::botch::Botch::botch = [&] (auto test, auto line, auto file) {
        fmt::print("BOTCH {}:{} -- \"{}\"\n", file, line, test);
        std::cout.flush();
        try {
            pybind11::exec(R"(
                import sys, code
                if sys.stdout.isatty():
                    c = code.InteractiveConsole(globals())
                    c.interact()
            )");
        } catch (pybind11::error_already_set ex) {
            fmt::print("{}\n", ex.what());
        }
        ::exit(-1);
    };

    std::function<void(uint64_t, pybind11::function)> f = [] (uint64_t delta, pybind11::function f) {
        ev::sched(delta, std::function<void()>(f));
    };
    conduit.def("sched", f);
}

static std::vector<std::string> search_paths{
    PIPELINE_SOURCE_PATH,
    PIPELINE_BUILD_PATH
};
void run_python_file(std::string fn)
{
    try {
        pybind11::dict global;
        global["__builtins__"] = pybind11::globals()["__builtins__"];
        global["conduit"] = getattr(pybind11::module::import("__main__"), "conduit");
        pybind11::eval_file<>(fn.c_str(), global);
    } catch (const std::exception &ex) {
        fmt::print(ex.what());
        BOTCH(true, "");
    }
}

static std::string input;
void load_ops(std::string fn)
{
    BOTCH(!input.empty(), "multiple input files not supported; exiting\n");
    std::ifstream ifs(fn);
    BOTCH(!ifs, "error opening input file \"{}\"; exiting\n", fn);
    input.assign(std::istreambuf_iterator<char>(ifs),
                 std::istreambuf_iterator<char>());
}

void dispatch_input(std::string fn)
{
    auto fp = pe::find_first_file(search_paths, fn);
    if (!fp) {
        fmt::print(std::cerr, "ERROR: could not find \"{}\" in:\n", fn);
        for (auto p : search_paths)
            fmt::print(std::cerr, "\t{}\n", p);
        ::exit(-1);
    }

    fmt::print("loading \"{}\"\n", *fp);

    static std::unordered_map<std::string, conduit::Function<void(std::string)>> dispatch_table = {
        {".ops", load_ops},
        {".py", run_python_file}
    };

    std::string ext;
    if (auto pos = fn.rfind('.'); pos != std::string::npos)
        ext = fn.substr(pos);
    if (auto entry = dispatch_table.find(ext); entry != dispatch_table.end()) {
        entry->second(*fp);
    } else {
        fmt::print(fmt::rgb(255, 0, 20), "Invalid input \"{}\"; exiting\n", fn);
        ::exit(-1);
    }
}

int main(int argc, const char *argv[])
{
    Py_Initialize();
    init();

    {
        // Registrar lifetime must be < than Python's, so introduce an
        // artificial scope
        auto &reg = *registrars.emplace_back(std::make_unique<conduit::Registrar>("pipeline"));

        for (auto &i : pe::SimInit::initializers())
            i.init(reg);

        for (int i = 1; i < argc; ++i)
            dispatch_input(argv[i]);

        bool end_simulation = false;
        reg.lookup<void()>("end simulation").hook([&end_simulation] {
            end_simulation = true;
        }, "main");

        conduit::ChannelInterface<void(std::string)> start = reg.lookup<void(std::string)>("start simulation", "main");
        ev::sched(1, start, input);

        set_channel_debug(true);

        auto &s = ev::detail::sched();
        while (!end_simulation) {
            BOTCH(s.empty(), "no events at {}\n", s.now());

            auto now = std::get<0>(s.front());
            auto &cycle = std::get<1>(s.front());

            fmt::print(fmt::rgb(120, 55, 220), "\n----{: ^80}----\n", now);

            for (auto iter = cycle.begin(); iter != cycle.end(); ++iter) 
                iter->ev();
            s.pop_front();
        }

        fmt::print(fmt::color::gold, "\n\nsimulation ended\n\n");
    }

    registrars.clear();
    Py_Finalize();
}
