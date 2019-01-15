
cmake_minimum_required(VERSION 3.11)
project(conduit-examples)

add_subdirectory(conduit EXCLUDE_FROM_ALL)
add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(benchmark EXCLUDE_FROM_ALL)
add_definitions(-D __STDC_LIMIT_MACROS -D __STDC_CONSTANT_MACROS -D __STDC_FORMAT_MACROS)

file(GLOB examples *.cpp)
foreach(example ${examples})
    get_filename_component(target ${example} NAME_WE)
    string(REGEX REPLACE "\\(|\\)" "_" target ${target})
    message("example ${target} ${example}")
    add_executable(${target} ${example})
    target_link_libraries(${target} conduit fmt)

    if (${target} MATCHES "benchmark$")
        target_link_libraries(${target} benchmark_main)
    endif()
    target_compile_options(${target} PUBLIC "-g")
endforeach()

