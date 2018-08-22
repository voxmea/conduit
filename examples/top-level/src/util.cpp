
#include <util.h>

#ifdef _MSC_VER
#include <Windows.h>
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma warning(pop)
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <execinfo.h>
#endif
#endif

#include <vector>
#include <fmt/format.h>
#include <conduit/expected.h>
#include <conduit/conduit-utility.h>

namespace pe
{
#if defined(_MSC_VER)
std::string get_exe_path()
{
    std::vector<char> buf(2048);
    #ifdef _UNICODE
    DWORD size = GetModuleFileName(NULL, (LPWSTR)&buf[0], static_cast<DWORD>(buf.size()));
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.to_bytes(std::wstring((wchar_t *)&buf[0], size));
    #else
    DWORD size = GetModuleFileName(NULL, &buf[0], static_cast<DWORD>(buf.size()));
    buf.resize(size);
    return std::string(buf.begin(), buf.end());
    #endif
}
#elif defined(__APPLE__)
std::string get_exe_path()
{
    std::vector<char> buf(2048);
    uint32_t size = buf.size();
    if (_NSGetExecutablePath(&buf[0], &size) == 0)
        buf.resize(size);
    else
        return "";
    return std::string(buf.begin(), buf.end());
}
#else
std::string get_exe_path()
{
    std::vector<char> buf(2048);
    ssize_t size = ::readlink("/proc/self/exe", &buf[0], buf.size());
    buf.resize(size);
    return std::string(buf.begin(), buf.end());
}
#endif

conduit::Expected<std::string, filesystem_error> find_first_file(std::vector<std::string> paths, std::string fn_)
{
    path fn = fn_;
    if (fn.is_absolute())
        return fn.string();
    for (path p : paths) {
        if (exists(p/fn))
            return (p/fn).string();
    }
    #ifdef _MSC_VER
    return conduit::make_unexpected<filesystem_error>(fmt::format("could not find \"{}\"", fn_));
    #else
    return conduit::make_unexpected<filesystem_error>(fmt::format("could not find \"{}\"", fn_), std::error_code{});
    #endif
}
}
