
cmake_minimum_required(VERSION 3.5)
project(conduit-tests)

# remove this after fixing BOTCH for MSVC when not compiling for c++17
if (MSVC)
    set(CMAKE_CXX_STANDARD 17)
    set(flags
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        )
    foreach(flag ${flags})
        string(REPLACE "/MD" "/MT" ${flag} "${${flag}}")
    endforeach()
else()
    set(CMAKE_CXX_STANDARD 14)
endif()

add_subdirectory(googletest)

include(GoogleTest)
file(GLOB src *.cpp)
add_executable(conduit-tests ${src})
target_include_directories(conduit-tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(conduit-tests PUBLIC gtest gmock gtest_main)
gtest_add_tests(TARGET conduit-tests)
