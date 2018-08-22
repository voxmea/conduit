
#ifndef PIPELINE_UTIL_H_
#define PIPELINE_UTIL_H_

#include <string>
#include <vector>
#include <conduit/expected.h>

#if __has_include(<filesystem>)
#include <filesystem>
namespace pe
{
    #ifdef _MSC_VER
    using path = std::experimental::filesystem::path;
    using filesystem_error = std::experimental::filesystem::filesystem_error;
    #else
    using path = std::filesystem::path;
    using filesystem_error = std::filesystem::filesystem_error;
    #endif
}
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace pe
{
    using path = std::experimental::filesystem::path;
    using filesystem_error = std::experimental::filesystem::filesystem_error;
}
#else
#error("could not find filesystem")
#endif

namespace pe {
std::string get_exe_path();
conduit::Expected<std::string, filesystem_error> find_first_file(std::vector<std::string> paths, std::string fn_);
}

#endif
