
cmake_minimum_required(VERSION 3.5)
project(analyze)

# set(CMAKE_MODULE_PATH /home/mgilbert/workspace/build/clang/3.9.1/lib/cmake/clang
                      # /home/mgilbert/workspace/build/clang/3.9.1/lib/cmake/llvm)

# find_package(Clang REQUIRED CONFIG)
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

find_package(Threads)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -static -static-libgcc -static-libstdc++")
endif()
if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fno-rtti")
endif()

set(clang_libs 
clangBasic
clangLex
clangParse
clangAST
clangDynamicASTMatchers
clangASTMatchers
clangSema
clangCodeGen
clangAnalysis
clangEdit
clangRewrite
clangARCMigrate
clangDriver
clangSerialization
clangRewriteFrontend
clangFrontend
clangFrontendTool
clangToolingCore
clangTooling
clangIndex
clangStaticAnalyzerCore
clangStaticAnalyzerCheckers
clangStaticAnalyzerFrontend
clangFormat
)

# get_cmake_property(_variableNames VARIABLES)
# foreach (_variableName ${_variableNames})
    # message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
link_directories(${LLVM_LIBRARY_DIR})
add_executable(analyze analyze.cpp)
if(MSVC)
    llvm_map_components_to_libnames(llvm_libs support core irreader libdriver profiledata transformutils coverage)
    target_link_libraries(analyze ${clang_libs} ${llvm_libs} version)
else()
    # link_directories(analyze /usr/lib/llvm-4.0/lib)
    # target_link_libraries(analyze "-Wl,--whole-archive" ${clang_libs} "-Wl,--no-whole-archive" LLVM-4.0)
    execute_process(COMMAND llvm-config --ldflags --libs --system-libs
                    OUTPUT_VARIABLE llvm_libs
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "\n" " " llvm_libs "${llvm_libs}")
    message(STATUS "llvm libs ${llvm_libs}")
    target_link_libraries(analyze "-Wl,--whole-archive" ${clang_libs} "-Wl,--no-whole-archive" ${llvm_libs})
    target_compile_options(analyze PUBLIC -fno-rtti)
endif()


