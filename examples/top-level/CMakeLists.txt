
cmake_minimum_required(VERSION 3.10)
project(conduit-top-level-example)

find_package(PythonInterp 3 REQUIRED)
find_package(PythonLibs 3 REQUIRED)
find_package(Threads REQUIRED)

# This has to come before compiling any submodules
include(CheckCXXSourceCompiles) 
if (UNIX AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    include(cmake/libc++.cmake)
    if (${clang_has_libcpp})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE True)

if (MSVC)
    add_definitions(-D _SCL_SECURE_NO_WARNINGS)
    add_definitions(-D _CRT_SECURE_NO_WARNINGS)
    set(PYBIND11_CPP_STANDARD /std:c++17)
else()
    set(PYBIND11_CPP_STANDARD -std=c++17)
endif()

add_definitions(-D __STDC_LIMIT_MACROS -D __STDC_CONSTANT_MACROS -D __STDC_FORMAT_MACROS)
add_definitions(-D USE_FIXVEC_POOL -D SOURCE_STRING_INTERNING -D CONDUIT_NO_LUA)
add_definitions(-D FMT_EXTENDED_COLORS)

add_subdirectory(pybind11 EXCLUDE_FROM_ALL)
add_subdirectory(fmt EXCLUDE_FROM_ALL)
add_subdirectory(conduit EXCLUDE_FROM_ALL)

file(GLOB_RECURSE src src/*.cpp)

# Simulator executable
add_executable(simulator ${src})
target_include_directories(simulator PUBLIC ${PYTHON_INCLUDE_DIRS}
                                            include)
target_link_libraries(simulator ${PYTHON_LIBRARIES}
                                pybind11::embed
                                fmt
                                conduit)
get_filename_component(python_lib_path ${PYTHON_LIBRARIES} DIRECTORY)
set_property(TARGET simulator PROPERTY CXX_STANDARD 17)
set_property(TARGET simulator PROPERTY CMAKE_CXX_EXTENSIONS off)
set_target_properties(simulator PROPERTIES CXX_VISIBILITY_PRESET hidden)

# generate version header
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/include/pipeline-version.h
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/gen-version-header.py ${CMAKE_CURRENT_BINARY_DIR}/include/pipeline-version.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen-version-header.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(pipeline-version ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/include/pipeline-version.h)
target_include_directories(simulator PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
add_dependencies(simulator pipeline-version)

if (MSVC)
    add_subdirectory(wineditline EXCLUDE_FROM_ALL)
    target_link_libraries(simulator edit_static)
else()
    target_link_libraries(simulator ${CMAKE_THREAD_LIBS_INIT} dl util readline)

    # this improves backtrace
    if(LINUX AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
        target_link_libraries(simulator "-Wl,-export-dynamic")
    elseif(LINUX)
        target_link_libraries(simulator "-Wl,-rdynamic")
    endif()

    if((${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang") AND (${clang_has_libcpp}))
        # follow symlinks first
        get_filename_component(clang_exe ${CMAKE_CXX_COMPILER} REALPATH)
        get_filename_component(clang_path ${clang_exe} DIRECTORY)
        target_link_libraries(simulator ${clang_path}/../lib/libc++experimental.a)
        set_target_properties(simulator PROPERTIES
                              INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${python_lib_path};${clang_path}/../lib;\$ORIGIN"
                              BUILD_WITH_INSTALL_RPATH True)
    else()
        include(cmake/stdc++fs.cmake)
        if (has_stdc++fs)
            target_link_libraries(simulator stdc++fs)
        endif()
        set_target_properties(simulator PROPERTIES
                              INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${python_lib_path};\$ORIGIN"
                              BUILD_WITH_INSTALL_RPATH True)
    endif()
endif()

