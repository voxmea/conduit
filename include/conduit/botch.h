
#ifndef CONDUIT_BOTCH_H_
#define CONDUIT_BOTCH_H_
#include <stdio.h>
#include <iostream>
#include <functional>


#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

#ifdef __has_include
#if __has_include(<fmt/format.h>)
// FIXME: If you get error about macro redefinition, it's your lucky day!!
//        Time to remove BOTCH_HAS_FMT from MSVC section of makefile and send email
//        indicating version of MSVC you're using to group.
#define BOTCH_HAS_FMT 1
#endif
#endif

#ifdef BOTCH_HAS_FMT
#include <fmt/format.h>
#define BOTCH_FMT_PRINT(...) fmt::print(__VA_ARGS__)
#else
#define BOTCH_FMT_PRINT(...)
#endif

#ifdef NO_BOTCH
#define BOTCH(...)
#else

#ifndef CONDUIT_NO_LUA
#include "repl.h"
#else
#include <exception>
#include <sstream>
namespace conduit {
    namespace lua {
        enum class ReplExit {
            ABORT,
            EXIT,
        }; 
        inline ReplExit start_lua_repl() { return ReplExit::EXIT;}
    }
    struct ConduitError : std::logic_error
    {
        using std::logic_error::logic_error;
        using std::logic_error::what;
    };
}
#endif

namespace conduit { namespace botch {

#if __cplusplus >= 201703 || (defined(_MSC_VER) && _MSC_VER >= 1913)
struct Botch
{
    static inline std::function<void(const char *, int, const char *)> botch = [] (const char *test, int line, const char *file)
    {
        #ifndef CONDUIT_NO_LUA
        std::cout << "ERROR: failed " << test << " at " << file << ":" << std::dec << line << std::endl;
        ::conduit::lua::ReplExit repl_exit_reason = ::conduit::lua::ReplExit::EXIT;
        if (isatty(fileno(stdout))) {
            std::cout << "Type exit or quit to exit the repl; abort to trigger abort handler." << std::endl;
            repl_exit_reason = ::conduit::lua::start_lua_repl();
        }
        if (repl_exit_reason == ::conduit::lua::ReplExit::ABORT) {
            ::abort();
        }
        ::exit(-1);
        #else
        {
            std::ostringstream stream;
            stream << file << "-" << line << ": " << test;
            throw ::conduit::ConduitError(stream.str());
        }
        #endif
    };
};

#else

struct Botch {

    static void botch(const char *test, int line, const char *file)
    {
        #ifndef CONDUIT_NO_LUA
        std::cout << "ERROR: failed " << test << " at " << file << ":" << std::dec << line << std::endl;
        ::conduit::lua::ReplExit repl_exit_reason = ::conduit::lua::ReplExit::EXIT;
        if (isatty(fileno(stdout))) {
            std::cout << "Type exit or quit to exit the repl; abort to trigger abort handler." << std::endl;
            repl_exit_reason = ::conduit::lua::start_lua_repl();
        }
        if (repl_exit_reason == ::conduit::lua::ReplExit::ABORT) {
            ::abort();
        }
        ::exit(-1);
        #else
        {
            std::ostringstream stream;
            stream << file << "-" << line << ": " << test;
            throw ::conduit::ConduitError(stream.str());
        }
        #endif
    }
};
#endif

} // namespace botch
} // namespace detail

#define BOTCH(test, ...)                                      \
    do {                                                      \
        if ((test)) {                                         \
            BOTCH_FMT_PRINT("\n"); BOTCH_FMT_PRINT(__VA_ARGS__); BOTCH_FMT_PRINT("\n"); \
            ::conduit::botch::Botch::botch(#test, __LINE__, __FILE__); \
        }                                                     \
    } while (0)

#endif

#endif
