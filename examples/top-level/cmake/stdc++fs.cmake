
set(CMAKE_REQUIRED_FLAGS "-std=c++17")
set(CMAKE_REQUIRED_LIBRARIES stdc++fs)
set(test_program
"#include <experimental/filesystem>
int main(int, char **argv) {std::experimental::filesystem::path path = argv[0];}")
CHECK_CXX_SOURCE_COMPILES("${test_program}"
                          has_stdc++fs)
unset(test_program)
unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_FLAGS)
