
set(CMAKE_REQUIRED_FLAGS "-stdlib=libc++")
CHECK_CXX_SOURCE_COMPILES("int main(int, char **) {}"
                          clang_has_libcpp)
unset(CMAKE_REQUIRED_FLAGS)
